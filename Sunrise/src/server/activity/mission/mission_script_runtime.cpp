#include "../../../middleware/bap/activity_message/sense_observation_packet.h"
#include "mission_script_event_batch.h"
#include "mission_script_runtime.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "../../../core/filesystem/path.h"
#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../../../middleware/crypto/sha256.h"
#include "../../../state/activity/mission/runtime.h"
#include "../../../state/activity/runtime.h"
#include "../../../state/activity_sdk/runtime.h"
#include "../../bap/runtime.h"
#include "../activity_sdk_mission_runtime.h"
#include "../host_runtime.h"
#include "mission_script_runtime_internal.h"
#include "mission_script_sdk_bridge.h"
#include "mission_script_vm.h"

namespace sunrise::server::activity::mission {

/**
 * Writes one bounded mission-script diagnostic line.
 * @param instance Binding the line reports, or null before one is bound.
 * @param fields Extra key=value pairs, appended as written.
 * @param error Free text; quoted and placed last so it never splits the pairs.
 */
void log_line(core::log::Level level,
              const RuntimeInstance* instance,
              std::string_view stage,
              std::string_view result,
              std::string_view fields,
              std::string_view error) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const unsigned long long session = instance == nullptr ? 0 : instance->view.binding.sessionId;
    const unsigned activityRow = instance == nullptr ? 0 : instance->identity.activityRow;
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=mission_script stage=%.*s result=%.*s session=%llu "
                                      "activity_row=%u",
                                      static_cast<int>(stage.size()),
                                      stage.data(),
                                      static_cast<int>(result.size()),
                                      result.data(),
                                      session,
                                      activityRow);
    if (written <= 0) {
        return;
    }
    std::size_t length = (std::min)(static_cast<std::size_t>(written), line.size() - 1);
    if (!fields.empty()) {
        const int piece = std::snprintf(line.data() + length,
                                        line.size() - length,
                                        " %.*s",
                                        static_cast<int>(fields.size()),
                                        fields.data());
        if (piece > 0) {
            length = (std::min)(length + static_cast<std::size_t>(piece), line.size() - 1);
        }
    }
    if (!error.empty()) {
        const int piece = std::snprintf(line.data() + length,
                                        line.size() - length,
                                        " error=\"%.*s\"",
                                        static_cast<int>(error.size()),
                                        error.data());
        if (piece > 0) {
            length = (std::min)(length + static_cast<std::size_t>(piece), line.size() - 1);
        }
    }
    core::log::write(core::log::Channel::server, level, {line.data(), length});
}

/**
 * Appends one host-state event for the script.
 * Host-state bursts are intentionally dynamic: authored Sense updates can raise more events than
 * any fixed bound without making the events invalid.
 */
void push_script_event(RuntimeInstance& instance, const host::Event& event) noexcept {
    if (instance.programStatus != ProgramStatus::loaded
        || !lua_vm::handles_event(instance.vm, event.kind)) {
        return;
    }
    try {
        instance.scriptEvents.push_back(event);
    } catch (const std::bad_alloc&) {
        log_line(core::log::Level::warn, &instance, "script_event", "allocation_failure");
    }
}

namespace {

enum class SourceStatus : std::uint8_t {
    ready,
    missing,
    fileError,
    tooLarge,
};

/** Stable result classes bound repeated attach logs. */
enum class AttachResult : std::uint8_t {
    none,
    catalogUnavailable,
    noActivityLink,
    sdkStatus,
    generatedWorldStatus,
    capacity,
    noScript,
    scriptFileError,
    sourceTooLarge,
    programError,
    ready,
};

/** One extra slot retains a capacity refusal beyond all runtime slots. */
constexpr std::size_t kAttachDiagnosticCapacity = host::kInstanceCapacity + 1;

/** One binding's last attach result limits repeated gate logs. */
struct AttachDiagnostic final {
    state::activity::SessionBinding binding{};
    sdk::Status sdkStatus{sdk::Status::notReady};
    generated::BindStatus generatedWorldStatus{generated::BindStatus::invalidBoundView};
    std::uint32_t activityRow{format::kAbsentIndex};
    AttachResult result{AttachResult::none};
    bool occupied{};
};

/** One queued mission event and the retry bookkeeping the drain uses. */
struct PendingMissionEvent final {
    host::Event event{};
    host::SenseObservationSnapshot sense{};
    host::ClientMessageSnapshot clientMessage{};
    std::uint64_t firstAttempt{};
    std::uint64_t nextAttempt{};
    std::uint32_t attempts{};
    bool missionSequenceObserved{};
    bool senseAvailable{};
    bool clientMessageAvailable{};
    bool callbackEligible{};
    bool eligibilityResolved{};
    bool occupied{};
};

/** One explicit reload may replace the source hash for its exact binding. */
struct ReloadAuthorization final {
    state::activity::SessionBinding binding{};
    mission_state::ProgramKey program{};
    bool occupied{};
};

std::array<RuntimeInstance, host::kInstanceCapacity> g_instances{};

[[nodiscard]] PlayerLife own_player_life(const RuntimeInstance& instance) noexcept {
    if (!instance.occupied || instance.playerLifeGeneration!=instance.view.activityClientGeneration)
        return PlayerLife::unknown;
    for (const auto& life:instance.playerLife)
        if (life.playerKey==instance.playerKey && instance.playerKey!=0) return life.life();
    return PlayerLife::unknown;
}

// A private mission's committed destination peers form its joined party. Public
// activity co-residents must never be treated as a fireteam. Missing or loading
// party members count as unknown and prevent the all-dead edge.
void publish_fireteam_life(std::uint64_t now) noexcept {
    for (auto& instance:g_instances) {
        if (!instance.occupied || instance.publicTarget || !instance.sessionRosterObserved
            || instance.programStatus!=ProgramStatus::loaded) continue;
        FireteamLife counts{};
        counts.add(own_player_life(instance));
        for (const auto& peer:instance.sessionRoster) {
            if (!peer.used) continue;
            PlayerLife life=PlayerLife::unknown;
            for (const auto& candidate:g_instances) {
                if (candidate.occupied && !candidate.publicTarget
                    && candidate.view.binding.sessionId==peer.sessionId
                    && candidate.view.binding.createdRevision==peer.createdRevision) {
                    life=own_player_life(candidate);break;
                }
            }
            counts.add(life);
        }
        if (instance.fireteamLifePublished && counts==instance.lastFireteamLife) continue;
        instance.lastFireteamLife=counts;instance.fireteamLifePublished=true;
        host::Event event{};
        event.kind=host::EventKind::fireteamState;event.binding=instance.view.binding;
        event.sequence=instance.missionStateRevision;
        event.sourceGeneration=instance.view.activityClientGeneration;
        event.missionSequence=instance.lastMissionSequence;event.tick=now;
        event.fireteamAlive=counts.alive;event.fireteamDead=counts.dead;event.fireteamUnknown=counts.unknown;
        push_script_event(instance,event);
        std::array<char,96> fields{};
        const int length=std::snprintf(fields.data(),fields.size(),"alive=%u dead=%u unknown=%u",
            unsigned(counts.alive),unsigned(counts.dead),unsigned(counts.unknown));
        if (length>0) log_line(core::log::Level::info,&instance,"fireteam_life","changed",
            {fields.data(),static_cast<std::size_t>(length)});
    }
}

void observe_player_life(RuntimeInstance& instance, const host::SenseObservationSnapshot& sense) noexcept {
    if (instance.playerLifeGeneration!=sense.sourceGeneration) {
        instance.playerLife={};instance.playerLifeGeneration=sense.sourceGeneration;
    }
    for (std::size_t i=0;i<sense.observationCount;++i) {
        const auto& observation=sense.observations[i];
        if (observation.key.slotType!=13 || observation.key.senseSchema!=0x80804F2F
            || observation.key.objectTag!=0x80FEB3DC || observation.key.slotIndex<5
            || observation.key.slotIndex>=21 || observation.firstValue>sense.valueCount
            || observation.valueCount>sense.valueCount-observation.firstValue) continue;
        update_player_life(instance.playerLife[observation.key.slotIndex-5],
            std::span(sense.values).subspan(observation.firstValue,observation.valueCount));
    }
}
std::array<AttachDiagnostic, kAttachDiagnosticCapacity> g_attachDiagnostics{};
std::vector<PendingMissionEvent> g_pendingMissionEvents{};
std::array<ReloadAuthorization, host::kInstanceCapacity> g_reloadAuthorizations{};
std::array<char, lua_vm::kSourceByteCapacity> g_source{};
core::path::Buffer g_scriptRoot{};
std::array<char, 1024> g_sdkLuaSearchPath{};
host::EventCursor g_eventCursor{};
host::MissionInputCursor g_missionInputCursor{};
bool g_enabled{};
bool g_pathReady{};
SRWLOCK g_lock{SRWLOCK_INIT};

template <std::size_t Capacity>
void copy_text(std::array<char, Capacity>& output, std::string_view value) noexcept {
    output = {};
    const std::size_t length = (std::min)(value.size(), output.size() - 1);
    std::copy_n(value.data(), length, output.data());
}

void note_vm_status(RuntimeInstance& instance,
                    std::string_view stage,
                    std::string_view status) noexcept {
    copy_text(instance.lastVmStage, stage);
    copy_text(instance.lastVmStatus, status);
}

/** Stable lowercase log token for one program status. */
[[nodiscard]] const char* program_status_name(ProgramStatus value) noexcept {
    switch (value) {
    case ProgramStatus::none:
        return "none";
    case ProgramStatus::loaded:
        return "loaded";
    case ProgramStatus::missing:
        return "missing";
    case ProgramStatus::fileError:
        return "file_error";
    case ProgramStatus::sourceTooLarge:
        return "source_too_large";
    case ProgramStatus::programError:
        return "program_error";
    }
    return "unknown";
}

/** Stable lowercase log token for one delivery stage. */
[[nodiscard]] const char* delivery_stage_name(DeliveryStage value) noexcept {
    switch (value) {
    case DeliveryStage::idle:
        return "idle";
    case DeliveryStage::awaitingHostCommit:
        return "awaiting_host_commit";
    case DeliveryStage::awaitingTransport:
        return "awaiting_transport";
    case DeliveryStage::awaitingCancel:
        return "awaiting_cancel";
    }
    return "unknown";
}

/** @return Stable panel-facing class for one retained attach result. */
[[nodiscard]] const char* attach_result_name(AttachResult value) noexcept {
    switch (value) {
    case AttachResult::none:
        return "none";
    case AttachResult::catalogUnavailable:
        return "catalog_unavailable";
    case AttachResult::noActivityLink:
        return "no_activity_link";
    case AttachResult::sdkStatus:
        return "sdk_status";
    case AttachResult::generatedWorldStatus:
        return "generated_world_status";
    case AttachResult::capacity:
        return "capacity";
    case AttachResult::noScript:
        return "no_script";
    case AttachResult::scriptFileError:
        return "script_file_error";
    case AttachResult::sourceTooLarge:
        return "source_too_large";
    case AttachResult::programError:
        return "program_error";
    case AttachResult::ready:
        return "ready";
    }
    return "unknown";
}

[[nodiscard]] ReloadAuthorization*
reload_authorization(const state::activity::SessionBinding& binding) noexcept {
    for (ReloadAuthorization& authorization : g_reloadAuthorizations) {
        if (authorization.occupied && same_binding(authorization.binding, binding)) {
            return &authorization;
        }
    }
    return nullptr;
}

[[nodiscard]] ReloadAuthorization* free_reload_authorization() noexcept {
    for (ReloadAuthorization& authorization : g_reloadAuthorizations) {
        if (!authorization.occupied) {
            return &authorization;
        }
    }
    return nullptr;
}

void clear_pending_event(PendingMissionEvent& pending) noexcept {
    SecureZeroMemory(&pending, sizeof(pending));
}

// The queue itself is private to this unit, so the delivery unit reaches it through this one
// entry point.
} // namespace

/** Retires every queued mission event that belongs to one binding. */
void clear_pending_events(const state::activity::SessionBinding& binding) noexcept {
    for (PendingMissionEvent& pending : g_pendingMissionEvents) {
        if (pending.occupied && same_binding(pending.event.binding, binding)) {
            clear_pending_event(pending);
        }
    }
}

namespace {

void clear_all_pending_events() noexcept {
    std::vector<PendingMissionEvent>{}.swap(g_pendingMissionEvents);
}

/** Keeps accepted values but removes every VM- and ActivityClient-generation-local decision. */
void reset_pending_events_for_reattach(const state::activity::SessionBinding& binding) noexcept {
    for (PendingMissionEvent& pending : g_pendingMissionEvents) {
        if (!pending.occupied || !same_binding(pending.event.binding, binding)) {
            continue;
        }
        pending.firstAttempt = 0;
        pending.nextAttempt = 0;
        pending.attempts = 0;
        pending.missionSequenceObserved = false;
        pending.callbackEligible = false;
        pending.eligibilityResolved = false;
    }
}

/** Retires rows as soon as authoritative State no longer owns their exact binding. */
void retire_unbound_pending_events() noexcept {
    for (PendingMissionEvent& pending : g_pendingMissionEvents) {
        if (pending.occupied && !state::activity::binding_matches(pending.event.binding)) {
            clear_pending_event(pending);
        }
    }
}

/** Clears every retained attach result. */
void clear_attach_diagnostics() noexcept {
    g_attachDiagnostics = {};
}

/** Emits one attach row with only known identity fields. */
void log_attach_line(core::log::Level level,
                     const state::activity::SessionBinding& binding,
                     std::string_view result,
                     std::uint32_t activityRow = format::kAbsentIndex) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    // BoundView rows are zero-based; every other Mission diagnostic presents ProgramIdentity's
    // one-based row. Keep the absent sentinel out of the log instead of incrementing it.
    const int written =
        activityRow == format::kAbsentIndex
            ? std::snprintf(line.data(),
                            line.size(),
                            "ev=mission_script stage=attach result=%.*s session=%llu",
                            static_cast<int>(result.size()),
                            result.data(),
                            static_cast<unsigned long long>(binding.sessionId))
            : std::snprintf(line.data(),
                            line.size(),
                            "ev=mission_script stage=attach result=%.*s session=%llu "
                            "activity_row=%u",
                            static_cast<int>(result.size()),
                            result.data(),
                            static_cast<unsigned long long>(binding.sessionId),
                            activityRow + 1);
    if (written > 0) {
        core::log::write(
            core::log::Channel::server,
            level,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1)});
    }
}

/** @return The retained result for one exact binding. */
[[nodiscard]] AttachDiagnostic*
find_attach_diagnostic(const state::activity::SessionBinding& binding) noexcept {
    for (AttachDiagnostic& diagnostic : g_attachDiagnostics) {
        if (diagnostic.occupied && same_binding(diagnostic.binding, binding)) {
            return &diagnostic;
        }
    }
    return nullptr;
}

/** @return One free result slot, or null when every Host slot is represented. */
[[nodiscard]] AttachDiagnostic* free_attach_diagnostic() noexcept {
    for (AttachDiagnostic& diagnostic : g_attachDiagnostics) {
        if (!diagnostic.occupied) {
            return &diagnostic;
        }
    }
    return nullptr;
}

/** Reports an attach result when it changes for the binding. */
void report_attach_result(
    const state::activity::SessionBinding& binding,
    AttachResult result,
    std::string_view name,
    std::uint32_t activityRow = format::kAbsentIndex,
    sdk::Status sdkStatus = sdk::Status::notReady,
    generated::BindStatus generatedWorldStatus = generated::BindStatus::invalidBoundView) noexcept {
    AttachDiagnostic* diagnostic = find_attach_diagnostic(binding);
    if (diagnostic != nullptr && diagnostic->result == result
        && diagnostic->activityRow == activityRow
        && (result != AttachResult::sdkStatus || diagnostic->sdkStatus == sdkStatus)
        && (result != AttachResult::generatedWorldStatus
            || diagnostic->generatedWorldStatus == generatedWorldStatus)) {
        return;
    }
    if (diagnostic == nullptr) {
        diagnostic = free_attach_diagnostic();
    }
    if (diagnostic != nullptr) {
        diagnostic->binding = binding;
        diagnostic->sdkStatus = sdkStatus;
        diagnostic->generatedWorldStatus = generatedWorldStatus;
        diagnostic->activityRow = activityRow;
        diagnostic->result = result;
        diagnostic->occupied = true;
    }
    log_attach_line(result == AttachResult::ready ? core::log::Level::info : core::log::Level::warn,
                    binding,
                    name,
                    activityRow);
}

/** Handle of the module holding this code, found from a data address inside it. */
[[nodiscard]] HMODULE owning_module() noexcept {
    HMODULE module = nullptr;
    const auto address = reinterpret_cast<LPCWSTR>(g_instances.data());
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           address,
                           &module)
        == FALSE) {
        return nullptr;
    }
    return module;
}

/** Frees one slot; its queued events are retired unless the caller keeps them for a reattach. */
void clear_instance(RuntimeInstance& instance, bool clearPending = true) noexcept {
    if (instance.occupied && clearPending) {
        clear_pending_events(instance.view.binding);
    } else if (instance.occupied) {
        reset_pending_events_for_reattach(instance.view.binding);
    }
    lua_vm::close(instance.vm);
    instance.worldView = {};
    instance.view = {};
    instance.identity = {};
    instance.programKey = {};
    instance.lastVmStage = {};
    instance.lastVmStatus = {};
    instance.eventsSeen = 0;
    instance.eventsCommitted = 0;
    instance.intentsTransportStaged = 0;
    instance.lastEventSequence = 0;
    instance.lastLoggedRevision = 0;
    instance.lastMissionSequence = 0;
    instance.missionPhase = 0;
    instance.missionStateRevision = 0;
    instance.activityStateRevision = 0;
    instance.durableIntentSequence = 0;
    instance.durableHostOutputRevision = 0;
    instance.expectedScriptableRevision = 0;
    instance.deliveryDeadline = 0;
    instance.firstIntentAttempt = 0;
    instance.nextIntentAttempt = 0;
    instance.firstStartAttempt = 0;
    instance.nextStartAttempt = 0;
    instance.pendingTimerEvent = {};
    instance.firstTimerAttempt = 0;
    instance.nextTimerAttempt = 0;
    instance.intentAttempts = 0;
    instance.startAttempts = 0;
    instance.timerAttempts = 0;
    instance.durablePendingIntentCount = 0;
    instance.lastIntentStatus = (std::numeric_limits<std::uint16_t>::max)();
    instance.initialStateRegion = -1;
    instance.activeRegion = -1;
    instance.programStatus = ProgramStatus::none;
    instance.deliveryStage = DeliveryStage::idle;
    instance.publicTarget = false;
    instance.playerKey = 0;
    instance.missionStateBound = false;
    instance.missionStarted = false;
    instance.missionStateFaulted = false;
    instance.initialStateDeclared = false;
    instance.initialStateSelected = false;
    instance.missionReattached = false;
    instance.startPending = false;
    instance.timerPending = false;
    instance.triggerOccupancy = {};
    instance.ghostObservations = {};
    instance.actorPathObservations = {};
    instance.squadObservations = {};
    instance.sceneObservations = {};
    instance.objectiveObservations = {};
    instance.sessionRoster = {};
    instance.sessionRosterObserved = false;
    instance.playerLife={};instance.playerLifeGeneration=0;
    instance.lastFireteamLife={};instance.fireteamLifePublished=false;
    std::vector<host::Event>{}.swap(instance.scriptEvents);
    instance.firstScriptEventAttempt = 0;
    instance.nextScriptEventAttempt = 0;
    instance.scriptEventAttempts = 0;
    instance.scriptEventRead = 0;
    instance.occupied = false;
}

[[nodiscard]] RuntimeInstance*
find_instance(const state::activity::SessionBinding& binding) noexcept {
    for (RuntimeInstance& instance : g_instances) {
        if (instance.occupied && same_binding(instance.view.binding, binding)) {
            return &instance;
        }
    }
    return nullptr;
}

[[nodiscard]] RuntimeInstance* free_instance() noexcept {
    for (RuntimeInstance& instance : g_instances) {
        if (!instance.occupied) {
            return &instance;
        }
    }
    return nullptr;
}

[[nodiscard]] bool is_active(const host::DiagnosticsSnapshot& diagnostics,
                             const state::activity::SessionBinding& binding) noexcept {
    for (std::size_t index = 0; index < diagnostics.instanceCount; ++index) {
        if (diagnostics.instances[index].active
            && same_binding(diagnostics.instances[index].binding, binding)) {
            return true;
        }
    }
    return false;
}

/** Drops results after their exact bindings leave the active Host set. */
void retire_attach_diagnostics(const host::DiagnosticsSnapshot& diagnostics) noexcept {
    for (AttachDiagnostic& diagnostic : g_attachDiagnostics) {
        if (diagnostic.occupied && !is_active(diagnostics, diagnostic.binding)) {
            diagnostic = {};
        }
    }
}

/** True when the link, the SDK view and the world view still match what the instance bound. */
[[nodiscard]] bool still_exact(RuntimeInstance& instance) noexcept {
    server::bap::ActivityLinkView link{};
    lua_vm::WorldGenerationIdentity worldGeneration{};
    return server::bap::activity_link_view(instance.view.binding, link)
           && link.publicTarget == instance.publicTarget
           && sdk::revalidate(instance.view,
                              instance.view.binding,
                              link.matchingLinks,
                              link.activityClientGeneration)
                  == sdk::Status::ready
           && instance.worldView.activity_sdk_view().catalog == instance.view.catalog
           && instance.worldView.activity_sdk_view().activityRow == instance.view.activityRow
           && instance.worldView.activity_sdk_view().activityClientGeneration
                  == instance.view.activityClientGeneration
           && sdk_bridge::world_generation_identity(instance.worldView, worldGeneration);
}

/**
 * Re-points one open program at the current ActivityClient generation.
 * @return True when the instance holds an exact view of the same program.
 */
[[nodiscard]] bool rebind_instance(RuntimeInstance& instance) noexcept {
    server::bap::ActivityLinkView link{};
    if (!server::bap::activity_link_view(instance.view.binding, link) || !link.joined) {
        return false;
    }
    const sdk::Snapshot catalog = sdk::snapshot();
    // A different SDK build is a different module, so that program opens again.
    if (catalog == nullptr || catalog != instance.view.catalog) {
        return false;
    }
    sdk::BoundView view{};
    const sdk::Selection selection{
        .binding = instance.view.binding,
        .matchingLinks = link.matchingLinks,
        .activityClientGeneration = link.activityClientGeneration,
    };
    if (sdk::resolve(catalog, selection, view) != sdk::Status::ready
        || view.activityRow != instance.view.activityRow) {
        return false;
    }
    generated::GeneratedWorldView worldView{};
    if (generated::resolve(view, worldView) != generated::BindStatus::ready) {
        return false;
    }
    instance.view = std::move(view);
    instance.worldView = std::move(worldView);
    instance.publicTarget = link.publicTarget;
    instance.playerKey = link.playerKey;
    instance.identity.playerKey = link.playerKey;
    instance.identity.publicTarget = link.publicTarget;
    // The bridge copies the world generation, so hand the program the rebuilt pair.
    return lua_vm::rebind(instance.vm,
                          instance.identity,
                          sdk_bridge::definition_api(instance.view, instance.worldView));
}

/** Folds the activity name into a lowercase file stem; other bytes become single underscores. */
[[nodiscard]] bool controller_stem(const sdk::Catalog& catalog,
                                   const format::Activity& activity,
                                   std::span<char> output) noexcept {
    if (output.size() < 2) {
        return false;
    }
    const std::string_view internal = catalog.string(activity.internalName);
    const std::string_view name =
        internal.empty() ? catalog.string(activity.displayName) : internal;
    std::size_t length = 0;
    bool separator = false;
    for (const unsigned char byte : name) {
        const bool digit = byte >= '0' && byte <= '9';
        const bool upper = byte >= 'A' && byte <= 'Z';
        const bool lower = byte >= 'a' && byte <= 'z';
        if (digit || upper || lower) {
            if (separator && length != 0) {
                if (length + 1 >= output.size()) {
                    return false;
                }
                output[length++] = '_';
            }
            separator = false;
            if (length + 1 >= output.size()) {
                return false;
            }
            output[length++] = static_cast<char>(upper ? byte + ('a' - 'A') : byte);
        } else {
            separator = true;
        }
    }
    if (length == 0) {
        constexpr std::string_view unnamed = "unnamed";
        if (unnamed.size() + 1 > output.size()) {
            return false;
        }
        std::copy(unnamed.begin(), unnamed.end(), output.begin());
        length = unnamed.size();
    }
    if (output[0] >= '0' && output[0] <= '9') {
        if (length + 2 > output.size()) {
            return false;
        }
        std::move_backward(output.begin(), output.begin() + length, output.begin() + length + 1);
        output[0] = '_';
        ++length;
    }
    output[length] = '\0';
    return true;
}

[[nodiscard]] bool controller_name(const sdk::Catalog& catalog,
                                   const format::Activity& activity,
                                   std::span<char> output) noexcept {
    std::array<char, 256> stem{};
    if (!controller_stem(catalog, activity, stem)) {
        return false;
    }
    const int length = std::snprintf(output.data(), output.size(), "%s.lua", stem.data());
    return length > 0 && static_cast<std::size_t>(length) < output.size();
}

/** Reads one whole file into the shared source buffer and says why it could not be read. */
[[nodiscard]] SourceStatus read_file(const core::path::Buffer& path,
                                     std::span<const char>& output) noexcept {
    output = {};
    HANDLE file = CreateFileW(path.chars.data(),
                              GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                   ? SourceStatus::missing
                   : SourceStatus::fileError;
    }
    LARGE_INTEGER size{};
    const bool sizeReady = GetFileSizeEx(file, &size) != FALSE;
    if (!sizeReady || size.QuadPart <= 0
        || static_cast<unsigned long long>(size.QuadPart) > g_source.size()) {
        CloseHandle(file);
        return sizeReady ? SourceStatus::tooLarge : SourceStatus::fileError;
    }
    DWORD read = 0;
    const auto requested = static_cast<DWORD>(size.QuadPart);
    const bool loaded =
        ReadFile(file, g_source.data(), requested, &read, nullptr) != FALSE && read == requested;
    CloseHandle(file);
    if (!loaded) {
        return SourceStatus::fileError;
    }
    output = {g_source.data(), read};
    return SourceStatus::ready;
}

/** Reads the authored script for the activity, falling back to the generated one when absent. */
[[nodiscard]] SourceStatus read_source(const sdk::Catalog& catalog,
                                       const format::Activity& activity,
                                       std::span<const char>& output) noexcept {
    std::array<char, 260> authoredName{};
    if (!controller_name(catalog, activity, authoredName)) {
        return SourceStatus::fileError;
    }
    std::array<wchar_t, 260> wideName{};
    const int wideLength = MultiByteToWideChar(CP_UTF8,
                                               MB_ERR_INVALID_CHARS,
                                               authoredName.data(),
                                               -1,
                                               wideName.data(),
                                               static_cast<int>(wideName.size()));
    core::path::Buffer authoredPath = g_scriptRoot;
    if (wideLength <= 1 || !core::path::append(authoredPath, L"\\")
        || !core::path::append(authoredPath, wideName.data())) {
        return SourceStatus::fileError;
    }
    const SourceStatus authored = read_file(authoredPath, output);
    if (authored != SourceStatus::missing) {
        return authored;
    }

    core::path::Buffer generatedPath = g_scriptRoot;
    std::array<wchar_t, 96> generatedName{};
    const int generatedLength = _snwprintf_s(generatedName.data(),
                                             generatedName.size(),
                                             _TRUNCATE,
                                             L"\\activities\\a_%04u_%08x.lua",
                                             activity.activityIndex,
                                             activity.definitionHash);
    if (generatedLength <= 0
        || !core::path::append(generatedPath,
                               {generatedName.data(), static_cast<std::size_t>(generatedLength)})) {
        return SourceStatus::fileError;
    }
    return read_file(generatedPath, output);
}

static_assert(mission_state::kSquadMemberCapacity == lua_vm::kSquadMemberCapacity);

/** Copies the complete VM outbox into typed State values in delivery order. */
[[nodiscard]] bool
snapshot_state_intents(const lua_vm::Vm& vm,
                       std::vector<mission_state::TypedIntent>& output) noexcept {
    return lua_vm::snapshot_intents(vm, output);
}

/** Builds the exact durable program identity from the already validated SDK view. */
[[nodiscard]] bool make_program_key(const RuntimeInstance& instance,
                                    const format::Activity& activity,
                                    std::span<const char> source,
                                    mission_state::ProgramKey& output) noexcept {
    output = {};
    if (instance.view.catalog == nullptr
        || activity.activityIndex
               > static_cast<std::uint32_t>((std::numeric_limits<std::int16_t>::max)())) {
        return false;
    }
    const std::span<const std::byte> digest = instance.view.catalog->sdk_build_sha256();
    if (digest.size() != output.sdkBuildSha256.size()) {
        return false;
    }
    std::copy(digest.begin(), digest.end(), output.sdkBuildSha256.begin());
    if (!sdk_bridge::world_program_generation_sha256(instance.worldView,
                                                     output.worldGenerationSha256)) {
        output = {};
        return false;
    }
    output.worldScenarioTag = instance.worldView.scenario_tag();
    if (!middleware::crypto::sha256::hash(std::as_bytes(source), output.scriptSourceSha256)) {
        output = {};
        return false;
    }
    output.activityDefinition = activity.definitionHash;
    output.activityIndex = static_cast<std::int16_t>(activity.activityIndex);
    output.publicTarget = instance.publicTarget;
    return true;
}

// The delivery unit accepts snapshots and faults instances too, so this group is shared.
} // namespace

bool controller_file_name(std::uint32_t oneBasedActivityRow, std::span<char> output) noexcept {
    if (oneBasedActivityRow == 0 || output.empty()) {
        return false;
    }
    const sdk::Snapshot catalog = sdk::snapshot();
    if (catalog == nullptr || oneBasedActivityRow - 1 >= catalog->activities().size()) {
        return false;
    }
    return controller_name(*catalog, catalog->activities()[oneBasedActivityRow - 1], output);
}

/** Copies one committed authoritative snapshot into the runtime's exact compare baseline. */
void accept_mission_state(RuntimeInstance& instance,
                          const mission_state::Snapshot& snapshot) noexcept {
    instance.missionStateRevision = snapshot.state.revision;
    instance.lastMissionSequence = snapshot.state.inputSequence;
    instance.activityStateRevision = snapshot.activityStateRevision;
    instance.missionPhase = snapshot.state.phase;
    instance.missionStarted = snapshot.state.started;
    instance.missionStateFaulted = snapshot.state.faulted;
    instance.durablePendingIntentCount = snapshot.state.pendingIntents.size();
    instance.durableIntentSequence = snapshot.state.pendingIntents.empty()
                                         ? mission_state::kAbsentIntentSequence
                                         : snapshot.state.pendingIntents.front().sequence;
    instance.durableHostOutputRevision =
        snapshot.state.pendingIntents.empty()
            ? mission_state::kAbsentHostOutputRevision
            : snapshot.state.pendingIntents.front().hostOutputRevision;
}

/** Marks the already-faulted VM in durable State when its exact compare still matches. */
void persist_mission_fault(RuntimeInstance& instance) noexcept {
    if (!instance.missionStateBound) {
        return;
    }
    mission_state::Snapshot snapshot{};
    const mission_state::Status status = mission_state::fault(
        instance.view.binding, instance.programKey, instance.missionStateRevision, snapshot);
    if (status == mission_state::Status::ready) {
        accept_mission_state(instance, snapshot);
        return;
    }
    log_line(core::log::Level::warn, &instance, "state_fault", mission_state::status_name(status));
}

/** Faults both the VM and the exact server-owned mission record. */
void fault_instance(RuntimeInstance& instance, std::string_view reason) noexcept {
    lua_vm::fault(instance.vm, reason);
    instance.programStatus = ProgramStatus::programError;
    persist_mission_fault(instance);
}

namespace {

/** Commits the VM's phase/revision/start transaction into exact server-owned State. */
[[nodiscard]] bool commit_mission_state(RuntimeInstance& instance,
                                        bool started,
                                        std::uint64_t nextInputSequence) noexcept {
    if (!instance.missionStateBound) {
        fault_instance(instance, "mission program has no authoritative State binding");
        return false;
    }
    lua_vm::Snapshot vm{};
    lua_vm::snapshot(instance.vm, vm);
    std::vector<mission_state::TypedIntent> pendingIntents{};
    std::array<mission_state::ScriptVariable, mission_state::kVariableCapacity> variables{};
    std::array<mission_state::MissionTimer, mission_state::kTimerCapacity> timers{};
    std::size_t variableCount = 0;
    std::size_t timerCount = 0;
    std::uint64_t nextTimerSequence = mission_state::kAbsentTimerSequence;
    std::uint64_t nextIntentKey = mission_state::kAbsentIntentKey;
    if (!snapshot_state_intents(instance.vm, pendingIntents)) {
        fault_instance(instance, "mission VM outbox snapshot was refused");
        return false;
    }
    if (!lua_vm::snapshot_durable_state(instance.vm,
                                        variables,
                                        variableCount,
                                        timers,
                                        timerCount,
                                        nextTimerSequence,
                                        nextIntentKey)) {
        fault_instance(instance, "mission VM durable-state snapshot was refused");
        return false;
    }
    const mission_state::CommitCandidate transaction{
        .variables = {variables.data(), variableCount},
        .timers = {timers.data(), timerCount},
        .pendingIntents = pendingIntents,
        .nextRevision = vm.stateRevision,
        .nextInputSequence = nextInputSequence,
        .nextTimerSequence = nextTimerSequence,
        .nextIntentKey = nextIntentKey,
        .phase = vm.phase,
        .started = started,
    };
    mission_state::Snapshot snapshot{};
    const mission_state::Status status = mission_state::commit(instance.view.binding,
                                                               instance.programKey,
                                                               instance.missionStateRevision,
                                                               instance.lastMissionSequence,
                                                               transaction,
                                                               snapshot);
    if (status == mission_state::Status::ready) {
        const std::uint32_t previousPhase = instance.missionPhase;
        accept_mission_state(instance, snapshot);
        if (instance.missionPhase != previousPhase) {
            queue_phase_entered(instance, previousPhase);
        }
        return true;
    }
    log_line(core::log::Level::warn, &instance, "state_commit", mission_state::status_name(status));
    fault_instance(instance, "authoritative mission State commit was refused");
    instance.programStatus = ProgramStatus::programError;
    return false;
}

/** Binds and restores the durable record before the opened program enters a callback. */
[[nodiscard]] bool bind_mission_state(RuntimeInstance& instance, std::uint64_t now) noexcept {
    mission_state::Snapshot snapshot{};
    mission_state::Status status =
        mission_state::bind(instance.view.binding, instance.programKey, snapshot);
    ReloadAuthorization* const authorization = reload_authorization(instance.view.binding);
    if (status == mission_state::Status::programMismatch && authorization != nullptr) {
        status = mission_state::rebind_program(
            instance.view.binding, authorization->program, instance.programKey, snapshot);
    }
    if (authorization != nullptr) {
        *authorization = {};
    }
    note_vm_status(instance, "state", mission_state::status_name(status));
    if (status != mission_state::Status::ready) {
        log_line(
            core::log::Level::warn, &instance, "state_bind", mission_state::status_name(status));
        lua_vm::fault(instance.vm, "authoritative mission State binding was refused");
        return false;
    }
    instance.missionStateBound = true;
    accept_mission_state(instance, snapshot);
    if (snapshot.state.variableCount > snapshot.state.variables.size()
        || snapshot.state.timerCount > snapshot.state.timers.size()) {
        fault_instance(instance, "authoritative mission State durable row count is invalid");
        return false;
    }
    std::vector<lua_vm::Intent> restoredIntents{};
    try {
        restoredIntents.reserve(snapshot.state.pendingIntents.size());
        for (const mission_state::PendingIntent& pending : snapshot.state.pendingIntents) {
            restoredIntents.push_back(pending.value);
        }
    } catch (const std::bad_alloc&) {
        fault_instance(instance, "authoritative mission State restore allocation failed");
        return false;
    }
    if (!lua_vm::restore_state(instance.vm,
                               snapshot.state.phase,
                               snapshot.state.revision,
                               {snapshot.state.variables.data(), snapshot.state.variableCount},
                               {snapshot.state.timers.data(), snapshot.state.timerCount},
                               snapshot.state.nextTimerSequence,
                               snapshot.state.nextIntentKey,
                               restoredIntents)) {
        fault_instance(instance, "authoritative mission State restore was refused");
        log_line(core::log::Level::warn, &instance, "state_restore", "vm_refused");
        return false;
    }
    if (instance.durableHostOutputRevision != mission_state::kAbsentHostOutputRevision) {
        instance.expectedScriptableRevision = instance.durableHostOutputRevision;
        instance.deliveryStage = DeliveryStage::awaitingHostCommit;
        instance.deliveryDeadline = deadline_after(now, kHostCommitTimeoutMs);
        instance.firstIntentAttempt = now;
        instance.intentAttempts = 1;
        host::InstanceSnapshot hostView{};
        if (host::instance_snapshot(instance.view.binding, hostView) && hostView.outputPending
            && hostView.outputKind == host::OutputKind::scriptableOverride
            && hostView.scriptableRevision == instance.expectedScriptableRevision) {
            instance.deliveryStage = DeliveryStage::awaitingTransport;
            instance.deliveryDeadline = deadline_after(now, kTransportTimeoutMs);
        } else if (hostView.scriptableRevision == instance.expectedScriptableRevision
                   && !hostView.outputPending
                   && hostView.scriptableTransportRevision != instance.expectedScriptableRevision) {
            instance.deliveryDeadline = now;
        }
    }
    if (snapshot.state.faulted) {
        lua_vm::fault(instance.vm, "authoritative mission State is faulted");
        log_line(core::log::Level::warn, &instance, "state_restore", "faulted");
        return false;
    }
    log_line(core::log::Level::debug,
             &instance,
             "state_restore",
             snapshot.state.started ? "started" : "ready");
    return true;
}

/**
 * Finishes one on_load call after same-session VM reattachment.
 * A restore is not a state transition, so an unchanged candidate must not spend a revision.
 * @return True when the program is running.
 */
[[nodiscard]] bool apply_load(RuntimeInstance& instance, lua_vm::CallStatus loaded) noexcept {
    lua_vm::Snapshot diagnostics{};
    lua_vm::snapshot(instance.vm, diagnostics);
    if (loaded != lua_vm::CallStatus::committed && loaded != lua_vm::CallStatus::noHandler) {
        instance.programStatus = ProgramStatus::programError;
        log_line(core::log::Level::warn,
                 &instance,
                 "load",
                 lua_vm::status_name(loaded),
                 {},
                 diagnostics.lastError.data());
        persist_mission_fault(instance);
        return false;
    }
    if (diagnostics.stateRevision != instance.missionStateRevision
        && !commit_mission_state(instance, true, instance.lastMissionSequence)) {
        return false;
    }
    instance.lastLoggedRevision = instance.missionStateRevision;
    return true;
}

enum class InitialStateGate : std::uint8_t {
    ready,
    pending,
    failed,
};

/** Selects the program-declared state once, then waits for its exact roster revision to publish. */
[[nodiscard]] InitialStateGate initial_state_gate(RuntimeInstance& instance) noexcept {
    if (!instance.initialStateDeclared) {
        return InitialStateGate::ready;
    }
    activity_sdk_mission::Snapshot seed{};
    const activity_sdk_mission::Status status =
        instance.initialStateSelected ? activity_sdk_mission::query(instance.view, seed)
                                      : activity_sdk_mission::select_state(
                                            instance.view, instance.initialStateRegion, {}, seed);
    if (status == activity_sdk_mission::Status::outputBusy) {
        return InitialStateGate::pending;
    }
    if (status != activity_sdk_mission::Status::ready || !seed.configured
        || seed.plan.effectiveRegion != static_cast<std::uint32_t>(instance.initialStateRegion)) {
        log_line(core::log::Level::warn,
                 &instance,
                 "initial_state",
                 activity_sdk_mission::status_name(status));
        fault_instance(instance, "program initial_state mission-seed selection was refused");
        return InitialStateGate::failed;
    }
    instance.initialStateSelected = true;
    // `plan.effectiveRegion` is the authored-state key; several authored states share one client
    // slice-set region, so the client link cannot report it. The lease revision reaching the
    // transport is the publication acknowledgement.
    return seed.publicationPending || seed.revision == 0 || seed.publishedRevision != seed.revision
               ? InitialStateGate::pending
               : InitialStateGate::ready;
}

/** Runs and durably commits a fresh program only after its initial-state gate is open. */
[[nodiscard]] bool start_program(RuntimeInstance& instance, std::uint64_t now) noexcept {
    const lua_vm::CallStatus started = lua_vm::start(instance.vm, now);
    note_vm_status(instance, "start", lua_vm::status_name(started));
    if (started != lua_vm::CallStatus::committed && started != lua_vm::CallStatus::noHandler) {
        instance.programStatus = ProgramStatus::programError;
        lua_vm::Snapshot diagnostics{};
        lua_vm::snapshot(instance.vm, diagnostics);
        log_line(core::log::Level::warn,
                 &instance,
                 "start",
                 lua_vm::status_name(started),
                 {},
                 diagnostics.lastError.data());
        persist_mission_fault(instance);
        return false;
    }
    if (!commit_mission_state(instance, true, instance.lastMissionSequence)) {
        return false;
    }
    instance.startPending = false;
    instance.lastLoggedRevision = instance.missionStateRevision;
    log_line(core::log::Level::info, &instance, "open", "ready");
    return true;
}

/** Opens the program for one slot, binds its durable record, then reattaches or starts it. */
[[nodiscard]] AttachResult open_program(RuntimeInstance& instance, std::uint64_t now) noexcept {
    const format::Activity* const activity = sdk::bound_activity(instance.view);
    if (activity == nullptr
        || !sdk_bridge::program_identity(instance.view, instance.publicTarget, instance.identity)) {
        instance.programStatus = ProgramStatus::programError;
        note_vm_status(instance, "open", "invalid_sdk_view");
        log_line(core::log::Level::warn, &instance, "open", "invalid_sdk_view");
        return AttachResult::programError;
    }
    instance.identity.sdkLuaSearchPath = g_sdkLuaSearchPath;
    instance.identity.playerKey = instance.playerKey;
    std::span<const char> source{};
    switch (read_source(*instance.view.catalog, *activity, source)) {
    case SourceStatus::missing:
        instance.programStatus = ProgramStatus::missing;
        note_vm_status(instance, "open", "no_script");
        log_line(core::log::Level::info, &instance, "open", "no_script");
        return AttachResult::noScript;
    case SourceStatus::fileError:
        instance.programStatus = ProgramStatus::fileError;
        note_vm_status(instance, "open", "file_error");
        log_line(core::log::Level::warn, &instance, "open", "file_error");
        return AttachResult::scriptFileError;
    case SourceStatus::tooLarge:
        instance.programStatus = ProgramStatus::sourceTooLarge;
        note_vm_status(instance, "open", "source_too_large");
        log_line(core::log::Level::warn, &instance, "open", "source_too_large");
        return AttachResult::sourceTooLarge;
    case SourceStatus::ready:
        break;
    }
    if (!make_program_key(instance, *activity, source, instance.programKey)) {
        std::fill(g_source.begin(), g_source.begin() + source.size(), '\0');
        instance.programStatus = ProgramStatus::programError;
        note_vm_status(instance, "state", "invalid_program_key");
        log_line(core::log::Level::warn, &instance, "open", "invalid_program_key");
        return AttachResult::programError;
    }
    const lua_vm::OpenStatus opened =
        lua_vm::open(instance.vm,
                     instance.identity,
                     sdk_bridge::definition_api(instance.view, instance.worldView),
                     source);
    std::fill(g_source.begin(), g_source.begin() + source.size(), '\0');
    note_vm_status(instance, "open", lua_vm::status_name(opened));
    if (opened != lua_vm::OpenStatus::ready) {
        instance.programStatus = ProgramStatus::programError;
        lua_vm::Snapshot diagnostics{};
        lua_vm::snapshot(instance.vm, diagnostics);
        log_line(core::log::Level::warn,
                 &instance,
                 "open",
                 lua_vm::status_name(opened),
                 {},
                 diagnostics.lastError.data());
        return AttachResult::programError;
    }
    instance.programStatus = ProgramStatus::loaded;
    instance.initialStateDeclared =
        lua_vm::initial_state_region(instance.vm, instance.initialStateRegion);
    if (instance.initialStateDeclared) {
        instance.activeRegion = instance.initialStateRegion;
    }
    if (!bind_mission_state(instance, now)) {
        instance.programStatus = ProgramStatus::programError;
        return AttachResult::programError;
    }
    if (instance.missionStarted) {
        instance.missionReattached = true;
        const lua_vm::CallStatus loaded = lua_vm::load(instance.vm, now);
        note_vm_status(instance, "load", lua_vm::status_name(loaded));
        if (!apply_load(instance, loaded)) {
            return AttachResult::programError;
        }
        log_line(core::log::Level::info, &instance, "open", "ready", "reason=state_reattached");
        return AttachResult::ready;
    }
    switch (initial_state_gate(instance)) {
    case InitialStateGate::failed:
        return AttachResult::programError;
    case InitialStateGate::pending:
        instance.startPending = true;
        log_line(core::log::Level::info, &instance, "initial_state", "publication_pending");
        return AttachResult::ready;
    case InitialStateGate::ready:
        break;
    }
    return start_program(instance, now) ? AttachResult::ready : AttachResult::programError;
}

/** Binds one host instance to a free slot once its link, SDK view and world view all resolve. */
void attach_instance(const host::InstanceSnapshot& hostInstance,
                     sdk::Snapshot catalog,
                     std::uint64_t now) noexcept {
    if (find_instance(hostInstance.binding) != nullptr) {
        return;
    }
    if (catalog == nullptr) {
        report_attach_result(
            hostInstance.binding, AttachResult::catalogUnavailable, "catalog_unavailable");
        return;
    }
    server::bap::ActivityLinkView link{};
    if (!server::bap::activity_link_view(hostInstance.binding, link)) {
        report_attach_result(
            hostInstance.binding, AttachResult::noActivityLink, "no_activity_link");
        return;
    }
    if (!link.joined) {
        report_attach_result(
            hostInstance.binding, AttachResult::noActivityLink, "activity_join_pending");
        return;
    }
    sdk::BoundView view{};
    const sdk::Selection selection{
        .binding = hostInstance.binding,
        .matchingLinks = link.matchingLinks,
        .activityClientGeneration = link.activityClientGeneration,
    };
    const sdk::Status status = sdk::resolve(catalog, selection, view);
    if (status != sdk::Status::ready) {
        report_attach_result(hostInstance.binding,
                             AttachResult::sdkStatus,
                             sdk::status_name(status),
                             format::kAbsentIndex,
                             status);
        return;
    }
    generated::GeneratedWorldView worldView{};
    const generated::BindStatus worldStatus = generated::resolve(view, worldView);
    if (worldStatus != generated::BindStatus::ready) {
        report_attach_result(hostInstance.binding,
                             AttachResult::generatedWorldStatus,
                             generated::status_name(worldStatus),
                             view.activityRow,
                             sdk::Status::notReady,
                             worldStatus);
        return;
    }
    RuntimeInstance* const instance = free_instance();
    if (instance == nullptr) {
        report_attach_result(
            hostInstance.binding, AttachResult::capacity, "capacity", view.activityRow);
        return;
    }
    instance->view = std::move(view);
    instance->worldView = std::move(worldView);
    instance->publicTarget = link.publicTarget;
    instance->playerKey = link.playerKey;
    instance->occupied = true;
    const AttachResult opened = open_program(*instance, now);
    report_attach_result(
        hostInstance.binding, opened, attach_result_name(opened), instance->view.activityRow);
}

/** Drops slots that no longer match, publishes the roster, and attaches active host instances. */
void synchronize_instances(std::uint64_t now) noexcept {
    host::DiagnosticsSnapshot diagnostics{};
    host::snapshot(diagnostics);
    retire_attach_diagnostics(diagnostics);
    retire_unbound_pending_events();
    for (RuntimeInstance& instance : g_instances) {
        const bool bindingActive =
            instance.occupied && is_active(diagnostics, instance.view.binding);
        const bool bindingRetained =
            instance.occupied && state::activity::binding_matches(instance.view.binding);
        // A generation change only stales the view, so rebind and keep the program.
        if (instance.occupied && bindingActive && bindingRetained && !still_exact(instance)
            && rebind_instance(instance)) {
            log_line(core::log::Level::debug, &instance, "rebind", "generation");
            continue;
        }
        if (instance.occupied && (!bindingActive || !still_exact(instance))) {
            log_line(core::log::Level::info, &instance, "close", "stale_generation");
            // Accepted mission inputs belong to the exact SessionBinding, not one ActivityClient
            // generation or one temporary link outage. Clear only after State replaces the exact
            // session generation; otherwise reattach must finish every already-accepted row.
            clear_instance(instance, !bindingRetained);
        }
    }
    std::array<state::activity::SessionRosterRow, state::activity::kSessionCapacity> roster{};
    std::size_t rosterCount = 0;
    static_cast<void>(state::activity::snapshot_session_roster(roster, rosterCount));
    for (RuntimeInstance& instance : g_instances) {
        if (instance.occupied) {
            push_session_roster_edges(instance, {roster.data(), rosterCount});
        }
    }
    const sdk::Snapshot catalog = sdk::snapshot();
    publish_fireteam_life(now);
    for (std::size_t index = 0; index < diagnostics.instanceCount; ++index) {
        if (diagnostics.instances[index].active) {
            attach_instance(diagnostics.instances[index], catalog, now);
        }
    }
}

/** Advances fresh programs only when their declared state roster has reached transport output. */
void service_pending_starts(std::uint64_t now) noexcept {
    for (RuntimeInstance& instance : g_instances) {
        if (!instance.occupied || !instance.startPending
            || instance.programStatus != ProgramStatus::loaded || instance.missionStarted) {
            continue;
        }
        switch (initial_state_gate(instance)) {
        case InitialStateGate::pending:
            break;
        case InitialStateGate::failed:
            instance.startPending = false;
            break;
        case InitialStateGate::ready:
            static_cast<void>(start_program(instance, now));
            break;
        }
    }
}

/** @return True for the host events that report an output's progress, not an input. */
[[nodiscard]] bool delivery_lifecycle_event(host::EventKind kind) noexcept {
    switch (kind) {
    case host::EventKind::authStateCommitted:
    case host::EventKind::authStateTransportStaged:
    case host::EventKind::authStateCanceled:
    case host::EventKind::incidentQueued:
    case host::EventKind::incidentTransportStaged:
    case host::EventKind::incidentCanceled:
    case host::EventKind::incidentRefused:
    case host::EventKind::scriptableOverrideCommitted:
    case host::EventKind::scriptableOverrideTransportStaged:
    case host::EventKind::scriptableOverrideCanceled:
    case host::EventKind::operatorRefused:
        return true;
    default:
        return false;
    }
}

/**
 * @return True for a row that arrives on the ordered mission-input feed and owns a sequence.
 * A host-state row must never answer true. It would consume a mission-input sequence it does not
 * own, which faults the binding on the next real input.
 */
[[nodiscard]] bool host_feed_row(host::EventKind kind) noexcept {
    switch (kind) {
    case host::EventKind::timerElapsed:
    case host::EventKind::effectResult:
    case host::EventKind::phaseEntered:
    case host::EventKind::triggerEntered:
    case host::EventKind::triggerExited:
    case host::EventKind::squadState:
    case host::EventKind::entitySpawned:
    case host::EventKind::entityDied:
    case host::EventKind::sceneFinished:
    case host::EventKind::objectiveProgress:
    case host::EventKind::sessionJoined:
    case host::EventKind::sessionLeft:
    case host::EventKind::playerTrigger:
    case host::EventKind::cinematicStarted:
    case host::EventKind::cinematicSkipRequested:
    case host::EventKind::cinematicTerminated:
    case host::EventKind::actorPathState:
    case host::EventKind::damageState:
    case host::EventKind::objectState:
    case host::EventKind::fireteamState:
    case host::EventKind::objectInteracted:
    case host::EventKind::ghostLinkState:
        return false;
    default:
        return true;
    }
}

/** True when the event may reach a callback for this instance's ActivityClient generation. */
[[nodiscard]] bool eligible_event(const RuntimeInstance& instance,
                                  const host::Event& event) noexcept {
    if (event.kind == host::EventKind::timerElapsed) {
        return true;
    }
    if (event.sourceGeneration != instance.view.activityClientGeneration) {
        return false;
    }
    return event.kind == host::EventKind::clientStateChanged
           || event.kind == host::EventKind::incidentReceived
           || event.kind == host::EventKind::clientMessageReceived
           || event.kind == host::EventKind::effectResult
           || event.kind == host::EventKind::phaseEntered
           || event.kind == host::EventKind::triggerEntered
           || event.kind == host::EventKind::triggerExited
           || event.kind == host::EventKind::squadState
           || event.kind == host::EventKind::entitySpawned
           || event.kind == host::EventKind::entityDied
           || event.kind == host::EventKind::sceneFinished
           || event.kind == host::EventKind::objectiveProgress
           || event.kind == host::EventKind::entitySlotsRequested
           || event.kind == host::EventKind::sessionJoined
           || event.kind == host::EventKind::sessionLeft
           || event.kind == host::EventKind::playerTrigger
           || event.kind == host::EventKind::cinematicStarted
           || event.kind == host::EventKind::actorPathState
           || event.kind == host::EventKind::damageState
           || event.kind == host::EventKind::objectState
           || event.kind == host::EventKind::fireteamState
           || event.kind == host::EventKind::objectInteracted
           || event.kind == host::EventKind::ghostLinkState
           || event.kind == host::EventKind::cinematicSkipRequested
           || event.kind == host::EventKind::cinematicTerminated
           || delivery_lifecycle_event(event.kind)
           || event.has_sense_observations();
}

/** Faults the instance unless the ordered mission input arrives with no gap, starting at one. */
[[nodiscard]] bool validate_mission_sequence(RuntimeInstance& instance,
                                             const host::Event& event) noexcept {
    if (event.kind != host::EventKind::senseUpdate
        && event.kind != host::EventKind::incidentReceived
        && event.kind != host::EventKind::clientStateChanged
        && event.kind != host::EventKind::entitySlotsRequested
        && event.kind != host::EventKind::clientMessageReceived) {
        return true;
    }
    if (instance.lastMissionSequence == 0) {
        if (event.missionSequence == 1) {
            return true;
        }
        fault_instance(instance, "activity mission input did not start at sequence one");
        log_line(core::log::Level::warn, &instance, "events", "initial_binding_gap");
        return false;
    }
    const std::uint64_t expected =
        instance.lastMissionSequence == (std::numeric_limits<std::uint64_t>::max)()
            ? 1
            : instance.lastMissionSequence + 1;
    if (event.missionSequence == expected) {
        return true;
    }
    fault_instance(instance, "activity mission input sequence has a gap");
    log_line(core::log::Level::warn, &instance, "events", "binding_gap");
    return false;
}

/** Records bounded Ember bridge/console fields while interaction semantics are verified. */
void log_ember_interaction_sense(RuntimeInstance& instance,
                          const host::SenseObservationSnapshot& sense) noexcept {
    for (std::size_t index = 0; index < sense.observationCount; ++index) {
        const auto& observation = sense.observations[index];
        const bool console = observation.key.slotType == 65
                             && observation.key.senseSchema == 0x80804D3EU;
        const bool bridge = observation.key.slotType == 23 && observation.key.slotIndex < 6
                            && observation.key.senseSchema == 0x80804F47U;
        // The Almighty's weapon ring: whether its two authored devices actually accept and
        // report position and power decides whether the beam can be driven from them at all.
        // Nothing observed these before, so a beam that did not change could not be told apart
        // from a device transition that never landed.
        const bool ring = observation.key.registryKey == 0xA3B76C64U
                          && observation.key.slotType == 23
                          && (observation.key.slotIndex == 48 || observation.key.slotIndex == 49)
                          && observation.key.senseSchema == 0x80804F47U;
        if ((observation.key.registryKey != 0xF6FFB59EU && !ring) || (!console && !bridge && !ring)
            || instance.emberInteractionReports >= 256
            || observation.firstValue > sense.valueCount
            || observation.valueCount > sense.valueCount - observation.firstValue) {
            continue;
        }
        for (std::size_t offset = 0; offset < observation.valueCount; ++offset) {
            const auto& value = sense.values[observation.firstValue + offset];
            if (!value.present || value.schemaRow != observation.key.schemaRow) {
                continue;
            }
            if (value.fieldOrdinal >= 9) continue;
            const std::size_t field =
                ring ? 64U + (observation.key.slotIndex - 48U) * 9U + value.fieldOrdinal
                     : (console ? 54U : observation.key.slotIndex * 9U) + value.fieldOrdinal;
            if (field >= instance.emberInteractionValues.size()) continue;
            const std::size_t word = field / 64U;
            const std::uint64_t bit = std::uint64_t{1} << (field % 64U);
            if ((instance.emberInteractionSeen[word] & bit) != 0
                && instance.emberInteractionValues[field] == value.unsignedValue) continue;
            instance.emberInteractionSeen[word] |= bit;
            instance.emberInteractionValues[field] = value.unsignedValue;
            if (instance.emberInteractionReports++ >= 256) return;
            std::array<char, 192> fields{};
            const int written = std::snprintf(
                fields.data(),
                fields.size(),
                "registry=%08x object=%08x slot=%u ordinal=%u bits=%u value=%llu sequence=%llu",
                observation.key.registryKey,
                observation.key.objectTag,
                static_cast<unsigned>(observation.key.slotIndex),
                static_cast<unsigned>(value.fieldOrdinal),
                static_cast<unsigned>(value.width),
                static_cast<unsigned long long>(value.unsignedValue),
                static_cast<unsigned long long>(observation.sequence));
            if (written > 0) {
                log_line(core::log::Level::info,
                         &instance,
                         ring ? "ring_device_sense"
                              : (console ? "ghost_link_sense" : "bridge_device_sense"),
                         "observed",
                         {fields.data(),
                          (std::min)(static_cast<std::size_t>(written), fields.size() - 1)});
            }
        }
    }
}

/** Runs one event through the VM, commits what it changed, and faults on a script failure. */
[[nodiscard]] lua_vm::CallStatus dispatch_event(RuntimeInstance& instance,
                                                const host::Event& event,
                                                const host::SenseObservationSnapshot* sense,
                                                const host::ClientMessageSnapshot* clientMessage,
                                                bool firstAttempt,
                                                std::uint64_t now) noexcept {
    if (instance.programStatus == ProgramStatus::missing
        && event.kind == host::EventKind::senseUpdate && sense != nullptr) {
        push_squad_edges(instance, *sense);
        return lua_vm::CallStatus::inactive;
    }
    if (instance.programStatus != ProgramStatus::loaded || !eligible_event(instance, event)) {
        return lua_vm::CallStatus::inactive;
    }
    if (firstAttempt) {
        ++instance.eventsSeen;
        instance.lastEventSequence = event.sequence;
    }
    // The three region numbers the script is about to read. A report restates only the leg it
    // moved, so `pending` and `current` read -1 on most reports and `held` is the one that says
    // where the client is standing.
    if (firstAttempt && event.kind == host::EventKind::clientStateChanged) {
        std::array<char, 64> legs{};
        const int written =
            std::snprintf(legs.data(),
                          legs.size(),
                          "held=%d pending=%d current=%d",
                          event.heldRegionIndex,
                          event.clientStateHasRegion ? event.regionIndex : -1,
                          event.clientStateHasCurrentRegion ? event.currentRegionIndex : -1);
        if (written > 0) {
            log_line(core::log::Level::debug,
                     &instance,
                     "client_state",
                     "legs",
                     {legs.data(), static_cast<std::size_t>(written)});
        }
    }
    const lua_vm::CallStatus status = lua_vm::dispatch(instance.vm, event, clientMessage, now);
    if (event.kind == host::EventKind::clientStateChanged) {
        // Pending-region reports can name the next slice while the player still
        // holds the old one. Match Lua's region choice and the committed after-image.
        if (event.heldRegionIndex >= 0) instance.activeRegion = event.heldRegionIndex;
        else if (event.currentRegionIndex >= 0) instance.activeRegion = event.currentRegionIndex;
    }
    if (firstAttempt && event.kind == host::EventKind::incidentReceived) {
        push_player_trigger(instance, event);
        push_cinematic(instance, event);
    }
    if (event.kind == host::EventKind::senseUpdate && sense != nullptr) {
        if (firstAttempt) {
            log_ember_interaction_sense(instance, *sense);
            observe_player_life(instance,*sense);
            publish_fireteam_life(now);
        }
        push_trigger_edges(instance, *sense);
        push_ghost_edges(instance, *sense);
        push_object_interaction_edges(instance, *sense);
        push_damage_edges(instance, *sense);
        push_actor_path_edges(instance, *sense);
        push_squad_edges(instance, *sense);
        push_scene_edges(instance, *sense);
        push_objective_edges(instance, *sense);
    }
    note_vm_status(instance, "event", lua_vm::status_name(status));
    if (firstAttempt && instance.eventsSeen == 1) {
        log_line(core::log::Level::info,
                 &instance,
                 "dispatch",
                 lua_vm::status_name(status),
                 event.kind == host::EventKind::senseUpdate            ? "sense"
                 : event.kind == host::EventKind::clientStateChanged   ? "client_state"
                 : event.kind == host::EventKind::incidentReceived     ? "incident"
                 : event.kind == host::EventKind::entitySlotsRequested ? "entity_slots_requested"
                 : event.kind == host::EventKind::timerElapsed         ? "timer"
                 : event.kind == host::EventKind::effectResult         ? "effect_result"
                                                                       : "client_message");
    }
    const std::uint64_t nextInputSequence =
        host_feed_row(event.kind) ? event.missionSequence : instance.lastMissionSequence;
    if (status == lua_vm::CallStatus::committed) {
        if (!commit_mission_state(instance, true, nextInputSequence)) {
            return lua_vm::CallStatus::scriptError;
        }
        ++instance.eventsCommitted;
        lua_vm::Snapshot diagnostics{};
        lua_vm::snapshot(instance.vm, diagnostics);
        if (event.kind == host::EventKind::clientStateChanged
            || diagnostics.stateRevision != instance.lastLoggedRevision) {
            instance.lastLoggedRevision = diagnostics.stateRevision;
            log_line(core::log::Level::debug,
                     &instance,
                     "event",
                     "committed",
                     event.kind == host::EventKind::senseUpdate          ? "sense"
                     : event.kind == host::EventKind::clientStateChanged ? "client_state"
                     : event.kind == host::EventKind::incidentReceived   ? "incident"
                     : event.kind == host::EventKind::entitySlotsRequested
                         ? "entity_slots_requested"
                     : event.kind == host::EventKind::timerElapsed ? "timer"
                     : event.kind == host::EventKind::effectResult ? "effect_result"
                                                                   : "client_message");
        }
        return status;
    }
    if (status == lua_vm::CallStatus::noHandler) {
        return commit_mission_state(instance, true, nextInputSequence)
                   ? status
                   : lua_vm::CallStatus::scriptError;
    }
    if (status == lua_vm::CallStatus::inactive) {
        return status;
    }
    lua_vm::Snapshot diagnostics{};
    lua_vm::snapshot(instance.vm, diagnostics);
    log_line(core::log::Level::warn,
             &instance,
             "event",
             lua_vm::status_name(status),
             {},
             diagnostics.lastError.data());
    persist_mission_fault(instance);
    return status;
}

/** Reads new host events, advances delivery, and queues only the lifecycle rows for scripts. */
void consume_delivery_events(std::uint64_t now) noexcept {
    host::EventRead events{};
    host::read_events_after(g_eventCursor, events);
    g_eventCursor = events.cursor;
    if (events.reset) {
        log_line(core::log::Level::warn, nullptr, "delivery", "event_feed_reset");
        return;
    }
    if (events.gap) {
        log_line(core::log::Level::warn, nullptr, "delivery", "event_feed_gap");
    }
    for (std::size_t index = 0; index < events.count; ++index) {
        RuntimeInstance* const instance = find_instance(events.events[index].binding);
        if (instance != nullptr) {
            observe_delivery_event(*instance, events.events[index], now);
            // Sense, client-state, incident and client-message rows reach the script through the
            // ordered mission-input feed. Only the delivery lifecycle rows belong in this queue.
            if (delivery_lifecycle_event(events.events[index].kind)) {
                push_script_event(*instance, events.events[index]);
            }
        }
    }
}

/** One free queue row, growing the queue by one when none is free; null when it cannot grow. */
[[nodiscard]] PendingMissionEvent* free_pending_event() noexcept {
    for (PendingMissionEvent& pending : g_pendingMissionEvents) {
        if (!pending.occupied) {
            return &pending;
        }
    }
    if (g_pendingMissionEvents.size() == g_pendingMissionEvents.max_size()) {
        return nullptr;
    }
    try {
        g_pendingMissionEvents.emplace_back();
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
    return &g_pendingMissionEvents.back();
}

/** Copies one accepted input and its values into a queue row; faults the instance when full. */
[[nodiscard]] bool queue_mission_event(RuntimeInstance* instance,
                                       const host::MissionInputEvent& input) noexcept {
    PendingMissionEvent* const pending = free_pending_event();
    if (pending == nullptr) {
        if (instance != nullptr) {
            fault_instance(*instance, "mission event queue allocation failed");
            clear_pending_events(instance->view.binding);
        }
        log_line(core::log::Level::warn, instance, "events", "allocation_failed");
        return false;
    }
    clear_pending_event(*pending);
    if (input.event.has_sense_observations()) {
        pending->senseAvailable =
            host::mission_input_sense_snapshot(input.sequence, pending->sense);
    }
    if (input.event.kind == host::EventKind::clientMessageReceived) {
        pending->clientMessageAvailable =
            host::mission_input_client_message_snapshot(input.sequence, pending->clientMessage);
    }
    pending->nextAttempt = 0;
    pending->event = input.event;
    if (instance != nullptr) {
        pending->callbackEligible = eligible_event(*instance, input.event);
        pending->eligibilityResolved = true;
    }
    pending->occupied = true;
    return true;
}

[[nodiscard]] constexpr bool mission_sequence_precedes(std::uint64_t left,
                                                       std::uint64_t right) noexcept {
    constexpr std::uint64_t halfRange = std::uint64_t{1} << 63U;
    return left != right && right - left < halfRange;
}

static_assert(mission_sequence_precedes((std::numeric_limits<std::uint64_t>::max)(), 1));
static_assert(!mission_sequence_precedes(1, (std::numeric_limits<std::uint64_t>::max)()));

/** @return True when the durable cursor already committed this retained input row. */
[[nodiscard]] bool mission_sequence_committed(std::uint64_t sequence,
                                              std::uint64_t committed) noexcept {
    return committed != 0
           && (sequence == committed || mission_sequence_precedes(sequence, committed));
}

/** True when the same binding still holds a queued row with an earlier mission sequence. */
[[nodiscard]] bool has_earlier_pending_event(const PendingMissionEvent& selected) noexcept {
    for (const PendingMissionEvent& pending : g_pendingMissionEvents) {
        if (pending.occupied && &pending != &selected
            && same_binding(pending.event.binding, selected.event.binding)
            && mission_sequence_precedes(pending.event.missionSequence,
                                         selected.event.missionSequence)) {
            return true;
        }
    }
    return false;
}

/** TODO: no caller. Decide whether the drain gate retires on this or on `binding_matches`. */
[[nodiscard]] bool host_binding_active(const state::activity::SessionBinding& binding) noexcept {
    host::InstanceSnapshot snapshot{};
    return host::instance_snapshot(binding, snapshot) && snapshot.active;
}

/** Dispatches queued rows in mission-sequence order and retires those no callback can take. */
void drain_pending_mission_events(std::uint64_t now) noexcept {
    bool progressed = false;
    do {
        progressed = false;
        for (PendingMissionEvent& pending : g_pendingMissionEvents) {
            if (!pending.occupied || now < pending.nextAttempt
                || has_earlier_pending_event(pending)) {
                continue;
            }
            RuntimeInstance* const instance = find_instance(pending.event.binding);
            if (instance == nullptr) {
                if (!state::activity::binding_matches(pending.event.binding)) {
                    clear_pending_event(pending);
                    progressed = true;
                }
                continue;
            }
            if (instance->programStatus != ProgramStatus::loaded) {
                clear_pending_event(pending);
                progressed = true;
                continue;
            }
            if (instance->startPending) {
                continue;
            }
            if (instance->timerPending) {
                continue;
            }
            if (mission_sequence_committed(pending.event.missionSequence,
                                           instance->lastMissionSequence)) {
                clear_pending_event(pending);
                progressed = true;
                continue;
            }
            if (!pending.missionSequenceObserved) {
                if (!validate_mission_sequence(*instance, pending.event)) {
                    clear_pending_events(instance->view.binding);
                    progressed = true;
                    break;
                }
                pending.missionSequenceObserved = true;
            }
            if (!pending.eligibilityResolved) {
                pending.callbackEligible = eligible_event(*instance, pending.event);
                pending.eligibilityResolved = true;
            }
            if (!pending.callbackEligible) {
                if (!commit_mission_state(
                        *instance, instance->missionStarted, pending.event.missionSequence)) {
                    clear_pending_events(instance->view.binding);
                    progressed = true;
                    break;
                }
                clear_pending_event(pending);
                progressed = true;
                continue;
            }
            if (pending.event.kind == host::EventKind::senseUpdate && !pending.senseAvailable) {
                fault_instance(*instance, "accepted mission Sense values were unavailable");
                clear_pending_events(instance->view.binding);
                log_line(core::log::Level::warn, instance, "events", "sense_unavailable");
                progressed = true;
                break;
            }
            if (pending.event.kind == host::EventKind::clientMessageReceived
                && !pending.clientMessageAvailable) {
                fault_instance(*instance,
                               "accepted mission client-message values were unavailable");
                clear_pending_events(instance->view.binding);
                log_line(core::log::Level::warn, instance, "events", "client_message_unavailable");
                progressed = true;
                break;
            }
            lua_vm::Intent intent{};
            if (instance->deliveryStage != DeliveryStage::idle
                || lua_vm::pending_intent(instance->vm, intent)) {
                continue;
            }
            const bool firstAttempt = pending.attempts == 0;
            if (firstAttempt) {
                pending.firstAttempt = now;
            }
            ++pending.attempts;
            const host::SenseObservationSnapshot* const sense =
                pending.event.kind == host::EventKind::senseUpdate && pending.senseAvailable
                    ? &pending.sense
                    : nullptr;
            const host::ClientMessageSnapshot* const clientMessage =
                pending.event.kind == host::EventKind::clientMessageReceived
                        && pending.clientMessageAvailable
                    ? &pending.clientMessage
                    : nullptr;
            static_cast<void>(
                dispatch_event(*instance, pending.event, sense, clientMessage, firstAttempt, now));
            clear_pending_event(pending);
            progressed = true;
        }
    } while (progressed);
}

/** @return True when one exact accepted sequence is already retained locally or in this read. */
[[nodiscard]] bool input_sequence_retained(const state::activity::SessionBinding& binding,
                                           std::uint64_t sequence,
                                           const host::MissionInputRead& inputs) noexcept {
    for (const PendingMissionEvent& pending : g_pendingMissionEvents) {
        if (pending.occupied && same_binding(pending.event.binding, binding)
            && pending.event.missionSequence == sequence) {
            return true;
        }
    }
    for (std::size_t index = 0; index < inputs.count; ++index) {
        if (same_binding(inputs.events[index].event.binding, binding)
            && inputs.events[index].event.missionSequence == sequence) {
            return true;
        }
    }
    return false;
}

/** @return True when every accepted but uncommitted sequence is present in this read or local
 * queue. */
[[nodiscard]] bool
outstanding_input_interval_complete(const state::activity::SessionBinding& binding,
                                    const mission_state::InputSequenceSnapshot& state,
                                    const host::MissionInputRead& inputs) noexcept {
    if (state.issued < state.committed) {
        return false;
    }
    const std::uint64_t outstanding = state.issued - state.committed;
    if (outstanding > g_pendingMissionEvents.size() + inputs.count) {
        return false;
    }
    std::uint64_t sequence = state.committed;
    for (std::uint64_t index = 0; index < outstanding; ++index) {
        ++sequence;
        if (!input_sequence_retained(binding, sequence, inputs)) {
            return false;
        }
    }
    return true;
}

/** Faults only retained bindings whose durable uncommitted interval is provably incomplete. */
void reconcile_input_feed_loss(const host::MissionInputRead& inputs) noexcept {
    host::DiagnosticsSnapshot hostState{};
    host::snapshot(hostState);
    for (std::size_t index = 0; index < hostState.instanceCount; ++index) {
        const host::InstanceSnapshot& hostInstance = hostState.instances[index];
        if (!state::activity::binding_matches(hostInstance.binding)) {
            continue;
        }
        mission_state::InputSequenceSnapshot inputState{};
        if (!mission_state::input_sequence_snapshot(hostInstance.binding, inputState)
            || inputState.faulted
            || outstanding_input_interval_complete(hostInstance.binding, inputState, inputs)) {
            continue;
        }
        mission_state::Snapshot snapshot{};
        const mission_state::Status status =
            mission_state::fault_input_feed(hostInstance.binding, snapshot);
        RuntimeInstance* const instance = find_instance(hostInstance.binding);
        if (status != mission_state::Status::ready) {
            log_line(core::log::Level::warn,
                     instance,
                     "events",
                     mission_state::status_name(status),
                     "reason=feed_gap_fault_refused");
            continue;
        }
        if (instance != nullptr) {
            accept_mission_state(*instance, snapshot);
            lua_vm::fault(instance->vm, "accepted mission input feed lost a row");
            instance->programStatus = ProgramStatus::programError;
        }
        log_line(core::log::Level::warn, instance, "events", "feed_gap_faulted");
        clear_pending_events(hostInstance.binding);
    }
}

/** Reads one page of the ordered feed and queues every row not yet committed. */
[[nodiscard]] bool consume_mission_input_page() noexcept {
    host::MissionInputRead inputs{};
    host::read_mission_inputs_after(g_missionInputCursor, inputs);
    if (inputs.reset) {
        g_missionInputCursor = {inputs.cursor.generation, 0};
        reconcile_input_feed_loss(inputs);
        log_line(core::log::Level::warn, nullptr, "events", "mission_feed_reset");
    }
    if (inputs.gap) {
        log_line(core::log::Level::warn, nullptr, "events", "mission_feed_gap");
        reconcile_input_feed_loss(inputs);
    }
    for (std::size_t index = 0; index < inputs.count; ++index) {
        const host::MissionInputEvent& input = inputs.events[index];
        RuntimeInstance* const instance = find_instance(input.event.binding);
        mission_state::InputSequenceSnapshot inputState{};
        const bool hasInputState =
            mission_state::input_sequence_snapshot(input.event.binding, inputState);
        if ((instance == nullptr && !state::activity::binding_matches(input.event.binding))
            || (instance != nullptr && instance->programStatus != ProgramStatus::loaded)
            || (hasInputState
                && (inputState.faulted
                    || mission_sequence_committed(input.event.missionSequence,
                                                  inputState.committed)))) {
            g_missionInputCursor.generation = inputs.cursor.generation;
            g_missionInputCursor.sequence = input.sequence;
            continue;
        }
        if (instance != nullptr
            && mission_sequence_committed(input.event.missionSequence,
                                          instance->lastMissionSequence)) {
            g_missionInputCursor.generation = inputs.cursor.generation;
            g_missionInputCursor.sequence = input.sequence;
            continue;
        }
        if (instance != nullptr) {
            lua_vm::Snapshot diagnostics{};
            lua_vm::snapshot(instance->vm, diagnostics);
            if (diagnostics.faulted) {
                g_missionInputCursor.generation = inputs.cursor.generation;
                g_missionInputCursor.sequence = input.sequence;
                continue;
            }
        }
        if (!queue_mission_event(instance, input)) {
            return false;
        }
        g_missionInputCursor.generation = inputs.cursor.generation;
        g_missionInputCursor.sequence = input.sequence;
    }
    return inputs.count == host::kMissionInputReadPageSize;
}

/**
 * Reads the ordered mission-input feed and drains the queue. The feed is unbounded and one read
 * copies at most a page, so a burst is consumed in the tick it arrives instead of a page a tick.
 */
void consume_mission_inputs(std::uint64_t now) noexcept {
    drain_pending_mission_events(now);
    while (consume_mission_input_page()) {
        drain_pending_mission_events(now);
    }
    drain_pending_mission_events(now);
}

/**
 * Settles the inputs of every instance whose activity has no script. No program will consume
 * them, and an unsettled row is retained by the Host feed until its capacity refuses new ones.
 */
void retire_scriptless_inputs() noexcept {
    for (RuntimeInstance& instance : g_instances) {
        if (!instance.occupied || instance.programStatus != ProgramStatus::missing) {
            continue;
        }
        mission_state::InputSequenceSnapshot inputState{};
        if (!mission_state::input_sequence_snapshot(instance.view.binding, inputState)
            || inputState.issued == inputState.committed) {
            continue;
        }
        const mission_state::Status status =
            mission_state::retire_unbound_inputs(instance.view.binding);
        if (status != mission_state::Status::ready) {
            log_line(core::log::Level::warn,
                     &instance,
                     "events",
                     mission_state::status_name(status),
                     "reason=scriptless_inputs_retained");
        }
    }
}

[[nodiscard]] bool has_pending_host_input(const RuntimeInstance& instance) noexcept {
    return std::any_of(g_pendingMissionEvents.begin(),
                       g_pendingMissionEvents.end(),
                       [&instance](const PendingMissionEvent& pending) noexcept {
                           return pending.occupied
                                  && same_binding(pending.event.binding, instance.view.binding);
                       });
}

[[nodiscard]] bool earlier_timer(const lua_vm::MissionTimer& left,
                                 const lua_vm::MissionTimer& right) noexcept {
    return left.deadlineTick < right.deadlineTick
           || (left.deadlineTick == right.deadlineTick && left.sequence < right.sequence);
}

/** Drain ordered derived events until output must commit or the batch budget is spent. */
void service_script_events(std::uint64_t now) noexcept {
    for (RuntimeInstance& instance : g_instances) {
        drain_script_event_batch([&] {
            if (!instance.occupied || instance.scriptEventRead >= instance.scriptEvents.size()
                || instance.programStatus != ProgramStatus::loaded || instance.startPending) {
                return false;
            }
            lua_vm::Intent pendingIntent{};
            return instance.deliveryStage == DeliveryStage::idle
                   && !lua_vm::pending_intent(instance.vm, pendingIntent);
        }, [&] {
            const bool firstAttempt = instance.scriptEventAttempts == 0;
            if (firstAttempt) instance.firstScriptEventAttempt = now;
            ++instance.scriptEventAttempts;
            host::Event& head = instance.scriptEvents[instance.scriptEventRead];
            if (head.kind == host::EventKind::actorPathState) {
                std::array<char, 128> fields{};
                const int length = std::snprintf(fields.data(), fields.size(),
                    "slot=%u queued_ms=%llu remaining=%zu",
                    static_cast<unsigned>(head.firstSlotIndex),
                    static_cast<unsigned long long>(now >= head.tick ? now - head.tick : 0),
                    instance.scriptEvents.size() - instance.scriptEventRead);
                if (length > 0 && static_cast<std::size_t>(length) < fields.size())
                    log_line(core::log::Level::info, &instance, "actor_callback", "dispatch",
                             {fields.data(), static_cast<std::size_t>(length)});
            }
            head.tick = now;
            static_cast<void>(dispatch_event(instance, head, nullptr, nullptr, firstAttempt, now));
            retire_script_event(instance);
        });
    }
}

/** Selects at most one due timer per instance without adding a second durable event feed. */
void service_timers(std::uint64_t now) noexcept {
    for (RuntimeInstance& instance : g_instances) {
        if (!instance.occupied || instance.programStatus != ProgramStatus::loaded
            || instance.startPending) {
            continue;
        }
        lua_vm::Intent pendingIntent{};
        if (instance.deliveryStage != DeliveryStage::idle
            || lua_vm::pending_intent(instance.vm, pendingIntent)) {
            continue;
        }
        if (!instance.timerPending) {
            mission_state::InputSequenceSnapshot inputState{};
            if (has_pending_host_input(instance)
                || !mission_state::input_sequence_snapshot(instance.view.binding, inputState)
                || inputState.faulted || inputState.issued != inputState.committed) {
                continue;
            }
            std::array<lua_vm::ScriptVariable, lua_vm::kVariableCapacity> variables{};
            std::array<lua_vm::MissionTimer, lua_vm::kTimerCapacity> timers{};
            std::size_t variableCount = 0;
            std::size_t timerCount = 0;
            std::uint64_t nextTimerSequence = mission_state::kAbsentTimerSequence;
            std::uint64_t nextIntentKey = mission_state::kAbsentIntentKey;
            if (!lua_vm::snapshot_durable_state(instance.vm,
                                                variables,
                                                variableCount,
                                                timers,
                                                timerCount,
                                                nextTimerSequence,
                                                nextIntentKey)) {
                fault_instance(instance, "mission timer snapshot was refused");
                continue;
            }
            const lua_vm::MissionTimer* selected = nullptr;
            for (std::size_t index = 0; index < timerCount; ++index) {
                if (timers[index].deadlineTick <= now
                    && (selected == nullptr || earlier_timer(timers[index], *selected))) {
                    selected = &timers[index];
                }
            }
            if (selected == nullptr) {
                continue;
            }
            instance.pendingTimerEvent = {};
            instance.pendingTimerEvent.binding = instance.view.binding;
            instance.pendingTimerEvent.timerName = selected->key;
            instance.pendingTimerEvent.sequence = selected->sequence;
            instance.pendingTimerEvent.tick = now;
            instance.pendingTimerEvent.sourceGeneration = instance.view.activityClientGeneration;
            instance.pendingTimerEvent.timerDeadlineTick = selected->deadlineTick;
            instance.pendingTimerEvent.timerSequence = selected->sequence;
            instance.pendingTimerEvent.missionSequence = instance.lastMissionSequence;
            instance.pendingTimerEvent.kind = host::EventKind::timerElapsed;
            instance.firstTimerAttempt = now;
            instance.nextTimerAttempt = now;
            instance.timerAttempts = 0;
            instance.timerPending = true;
        }
        const bool firstAttempt = instance.timerAttempts == 0;
        ++instance.timerAttempts;
        static_cast<void>(dispatch_event(
            instance, instance.pendingTimerEvent, nullptr, nullptr, firstAttempt, now));
        instance.pendingTimerEvent = {};
        instance.firstTimerAttempt = 0;
        instance.nextTimerAttempt = 0;
        instance.timerAttempts = 0;
        instance.timerPending = false;
    }
}

[[nodiscard]] std::size_t
pending_event_count(const state::activity::SessionBinding& binding) noexcept {
    std::size_t count = 0;
    for (const PendingMissionEvent& pending : g_pendingMissionEvents) {
        count += pending.occupied && same_binding(pending.event.binding, binding) ? 1U : 0U;
    }
    return count;
}

/** Copies one instance and its VM counters into the panel-facing diagnostics row. */
void copy_diagnostics(const RuntimeInstance& instance, InstanceDiagnostics& output) noexcept {
    output = {};
    output.binding = instance.view.binding;
    output.activityId = instance.identity.activityId;
    copy_text(output.programStatus, program_status_name(instance.programStatus));
    copy_text(output.deliveryStage, delivery_stage_name(instance.deliveryStage));
    output.lastVmStage = instance.lastVmStage;
    output.lastVmStatus = instance.lastVmStatus;
    output.eventsSeen = instance.eventsSeen;
    output.eventsCommitted = instance.eventsCommitted;
    output.lastEventSequence = instance.lastEventSequence;
    output.lastMissionSequence = instance.lastMissionSequence;
    output.missionStateRevision = instance.missionStateRevision;
    output.activityStateRevision = instance.activityStateRevision;
    output.expectedScriptableRevision = instance.expectedScriptableRevision;
    output.activityRow = instance.identity.activityId[0] != '\0' ? instance.identity.activityRow
                         : instance.view.activityRow == format::kAbsentIndex
                             ? 0
                             : instance.view.activityRow + 1;
    output.intentAttempts = instance.intentAttempts;
    output.startAttempts = instance.startAttempts;
    output.pendingEvents = pending_event_count(instance.view.binding);
    output.publicTarget = instance.publicTarget;
    output.missionStateBound = instance.missionStateBound;
    output.missionStarted = instance.missionStarted;
    output.startPending = instance.startPending;

    lua_vm::Snapshot vm{};
    lua_vm::snapshot(instance.vm, vm);
    output.lastVmError = vm.lastError;
    output.vmStateRevision = vm.stateRevision;
    output.vmCallbacks = vm.callbacks;
    output.vmCommittedCallbacks = vm.committedCallbacks;
    output.vmRefusedCallbacks = vm.refusedCallbacks;
    output.intentsTransportStaged = instance.intentsTransportStaged;
    output.phase = vm.phase;
    output.pendingIntents = vm.pendingIntents;
    output.vmActive = vm.active;
    output.vmFaulted = vm.faulted;
}

/** Copies one retained attach result without exposing its internal enum or catalog pointers. */
void copy_attach_diagnostics(const AttachDiagnostic& source, AttachDiagnostics& output) noexcept {
    output = {};
    output.binding = source.binding;
    copy_text(output.result, attach_result_name(source.result));
    copy_text(output.detail,
              source.result == AttachResult::sdkStatus ? sdk::status_name(source.sdkStatus)
              : source.result == AttachResult::generatedWorldStatus
                  ? generated::status_name(source.generatedWorldStatus)
                  : attach_result_name(source.result));
    if (source.activityRow != format::kAbsentIndex) {
        output.activityRow = source.activityRow + 1;
        output.hasActivityRow = true;
    }
}

} // namespace

/** Clears all state, reads the enable switch, and resolves the script and SDK Lua paths. */
void initialize() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    clear_attach_diagnostics();
    clear_all_pending_events();
    for (RuntimeInstance& instance : g_instances) {
        clear_instance(instance);
    }
    g_enabled = core::settings::get().server.activation.missionScripting;
    g_pathReady = false;
    g_eventCursor = host::current_event_cursor();
    g_missionInputCursor = host::current_mission_input_cursor();
    // Re-read retained accepted inputs after runtime restart. Durable per-binding cursors below
    // suppress callbacks that already committed; an evicted predecessor still faults as a gap.
    g_missionInputCursor.sequence = 0;
    if (!g_enabled) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    HMODULE const module = owning_module();
    core::path::Buffer sdkLua{};
    core::path::Buffer scriptLua{};
    // Generated modules come first, so a controller cannot shadow one by filename.
    if (module == nullptr || !core::path::artifact_directory(module, g_scriptRoot)
        || !core::path::artifact_directory(module, sdkLua)
        || !core::path::artifact_directory(module, scriptLua)
        || !core::path::append(sdkLua, L"\\sdk\\lua\\?.lua")
        || !core::path::append(scriptLua, L"\\scripts\\?.lua") || !core::path::append(sdkLua, L";")
        || !core::path::append(sdkLua, {scriptLua.chars.data(), scriptLua.length})
        || !core::path::append(g_scriptRoot, L"\\scripts")) {
        log_line(core::log::Level::warn, nullptr, "initialize", "path_error");
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    const int sdkLuaBytes = WideCharToMultiByte(CP_UTF8,
                                                WC_ERR_INVALID_CHARS,
                                                sdkLua.chars.data(),
                                                -1,
                                                g_sdkLuaSearchPath.data(),
                                                static_cast<int>(g_sdkLuaSearchPath.size()),
                                                nullptr,
                                                nullptr);
    if (sdkLuaBytes <= 1) {
        g_sdkLuaSearchPath = {};
        log_line(core::log::Level::warn, nullptr, "initialize", "sdk_lua_path_error");
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    g_pathReady = true;
    log_line(core::log::Level::info, nullptr, "initialize", "enabled");
    ReleaseSRWLockExclusive(&g_lock);
}

/** One pass: attach, start, events, timers, then delivery for every open instance. */
void service(std::uint64_t now) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (!g_enabled || !g_pathReady) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    synchronize_instances(now);
    service_pending_starts(now);
    consume_delivery_events(now);
    consume_mission_inputs(now);
    retire_scriptless_inputs();
    service_timers(now);
    service_script_events(now);
    for (RuntimeInstance& instance : g_instances) {
        if (instance.occupied) {
            if (instance.programStatus == ProgramStatus::programError) {
                reconcile_terminal_delivery(instance);
            } else {
                dispatch_intent(instance, now);
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Copies every open instance row and every retained attach row for the panel. */
void snapshot(DiagnosticsSnapshot& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    output.enabled = g_enabled;
    output.pathReady = g_pathReady;
    for (const RuntimeInstance& instance : g_instances) {
        if (instance.occupied && output.instanceCount < output.instances.size()) {
            copy_diagnostics(instance, output.instances[output.instanceCount++]);
        }
    }
    for (const AttachDiagnostic& diagnostic : g_attachDiagnostics) {
        if (diagnostic.occupied && output.attachCount < output.attaches.size()) {
            copy_attach_diagnostics(diagnostic, output.attaches[output.attachCount++]);
        }
    }
    ReleaseSRWLockShared(&g_lock);
}

/** Closes every instance and authorizes one program replacement so the next attach recompiles. */
bool reload() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (!g_enabled || !g_pathReady) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    bool reloaded = true;
    for (RuntimeInstance& instance : g_instances) {
        if (!instance.occupied) {
            continue;
        }
        log_line(core::log::Level::info, &instance, "reload", "requested");
        if (instance.missionStateBound && instance.missionStateFaulted) {
            mission_state::Snapshot recovered{};
            const mission_state::Status status =
                mission_state::recover(instance.view.binding,
                                       instance.programKey,
                                       instance.missionStateRevision,
                                       recovered);
            if (status != mission_state::Status::ready) {
                log_line(core::log::Level::warn,
                         &instance,
                         "reload",
                         mission_state::status_name(status));
                reloaded = false;
                continue;
            }
            accept_mission_state(instance, recovered);
            clear_pending_events(instance.view.binding);
        }
        ReloadAuthorization* authorization = free_reload_authorization();
        if (authorization == nullptr) {
            log_line(core::log::Level::warn, &instance, "reload", "capacity");
            reloaded = false;
            continue;
        }
        authorization->binding = instance.view.binding;
        authorization->program = instance.programKey;
        authorization->occupied = true;
        clear_instance(instance, false);
    }
    clear_attach_diagnostics();
    ReleaseSRWLockExclusive(&g_lock);
    return reloaded;
}

/** Closes every instance and clears every global this unit owns. */
void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    for (RuntimeInstance& instance : g_instances) {
        clear_instance(instance);
    }
    clear_all_pending_events();
    clear_attach_diagnostics();
    std::fill(g_source.begin(), g_source.end(), '\0');
    g_scriptRoot = {};
    g_sdkLuaSearchPath = {};
    g_eventCursor = {};
    g_missionInputCursor = {};
    g_reloadAuthorizations = {};
    g_enabled = false;
    g_pathReady = false;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace sunrise::server::activity::mission
