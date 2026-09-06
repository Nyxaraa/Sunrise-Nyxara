#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

#include "../../middleware/bap/activity_message/sensor_auth_update.h"
#include "../../middleware/bap/activity_message/mission_auth_patch.h"
#include "../../middleware/bap/activity_message/squad_objective_auth.h"
#include "../../middleware/bap/activity_message/combatant_path_auth.h"
#include "../../middleware/bap/activity_message/combatant_action_auth.h"
#include "../../middleware/bap/activity_message/combatant_delivery_auth.h"
#include "../../middleware/bap/activity_message/combatant_retire_auth.h"
#include "../../state/activity/mission/runtime.h"
#include "../../core/logging/log.h"
#include "../../state/activity/runtime.h"
#include "host_runtime_internal.h"

namespace sunrise::server::activity::host {
namespace {

namespace auth = middleware::bap::activity_message::scriptable_auth;
namespace scene = middleware::bap::activity_message::sensor_auth_update;
namespace squad = middleware::bap::activity_message::squad_auth;
using namespace detail;

/** Authored-scene activation generations are positive signed 32-bit values. */
constexpr std::uint32_t kMaximumAuthoredSceneGeneration = 0x7FFFFFFFU;

/** @return The next positive authored-scene generation without changing the guard. */
[[nodiscard]] bool next_authored_scene_generation(std::uint32_t last,
                                                  std::uint32_t& output) noexcept {
    output = 0;
    if (last >= kMaximumAuthoredSceneGeneration) {
        return false;
    }
    output = last + 1;
    return true;
}

/** Encodes the fixed type-43 body with no dependencies, scalar input, or events. */
[[nodiscard]] bool encode_authored_scene(std::uint32_t generation,
                                         std::span<std::byte> output,
                                         std::size_t& written) noexcept {
    written = 0;
    if (generation == 0 || generation > kMaximumAuthoredSceneGeneration
        || output.size() < scene::kAuthoredSceneAuthByteCount) {
        return false;
    }
    const std::uint32_t wireGeneration = generation + 0x80000000U;
    output[0] = static_cast<std::byte>(wireGeneration >> 24U);
    output[1] = static_cast<std::byte>(wireGeneration >> 16U);
    output[2] = static_cast<std::byte>(wireGeneration >> 8U);
    output[3] = static_cast<std::byte>(wireGeneration);
    std::fill(output.begin() + 4, output.begin() + scene::kAuthoredSceneAuthByteCount, std::byte{});
    written = scene::kAuthoredSceneAuthByteCount;
    return true;
}

/** @return True when both values name the same full ClientRef slot. */
[[nodiscard]] bool same_target(const ScriptableTarget& left,
                               const ScriptableTarget& right) noexcept {
    return left.objectTag == right.objectTag && left.registryKey == right.registryKey
           && left.authSchema == right.authSchema && left.rosterGroupIndex == right.rosterGroupIndex
           && left.rosterSlotOffset == right.rosterSlotOffset && left.slotIndex == right.slotIndex
           && left.sdkObjectIndex == right.sdkObjectIndex
           && left.stateLocalRegion == right.stateLocalRegion && left.slotType == right.slotType
           && left.stateLocalRoster == right.stateLocalRoster;
}

/** @return True when both routes resolve to the same wire ClientRef. */
[[nodiscard]] bool same_client_ref(const ScriptableTarget& left,
                                   const ScriptableTarget& right) noexcept {
    return left.objectTag == right.objectTag && left.registryKey == right.registryKey
           && left.slotIndex == right.slotIndex && left.slotType == right.slotType;
}

/** @return True when both retained generated groups are byte-for-byte identical in used fields. */
[[nodiscard]] bool same_group(const state::build_data::scenarios::RosterGroup& left,
                              const state::build_data::scenarios::RosterGroup& right) noexcept {
    if (!state::build_data::scenarios::valid_roster_group(left)
        || !state::build_data::scenarios::valid_roster_group(right)
        || left.registryKey != right.registryKey || left.objectTag != right.objectTag
        || left.slotCount != right.slotCount) {
        return false;
    }
    for (std::size_t index = 0; index < left.slotCount; ++index) {
        if (left.slotTypes[index] != right.slotTypes[index]
            || left.slotFlags[index] != right.slotFlags[index]
            || left.slotIndices[index] != right.slotIndices[index]) {
            return false;
        }
    }
    return true;
}

/** @return True when one caller still owns the exact unarmed lane held by this instance. */
[[nodiscard]] bool same_reservation(const ScriptableOutputReservation& left,
                                    const ScriptableOutputReservation& right) noexcept {
    return same_binding(left.binding, right.binding)
           && left.resetGeneration == right.resetGeneration && left.token == right.token
           && left.revision == right.revision && left.intentSequence == right.intentSequence
           && left.resetGeneration != 0 && left.token != 0 && left.revision != 0;
}

/** Clears one unarmed reservation without touching the committed output counter. */
void clear_reservation(Instance& instance) noexcept {
    instance.scriptableReservation = {};
    instance.view.scriptableReservedRevision = 0;
    instance.view.scriptableReservationPending = false;
}

/** @return True when one carried group contains the target's exact selected auth slot. */
[[nodiscard]] bool
valid_state_local_group(const ScriptableTarget& target,
                        const state::build_data::scenarios::RosterGroup& group) noexcept {
    const std::size_t slot = target.rosterSlotOffset;
    return target.stateLocalRoster && target.stateLocalRegion >= 0
           && target.rosterGroupIndex == kGeneratedRosterGroupIndex
           && target.sdkObjectIndex != kNoSdkObjectIndex
           && state::build_data::scenarios::valid_roster_group(group) && group.objectTag != 0
           && group.objectTag == target.objectTag && group.registryKey == target.registryKey
           && slot < group.slotCount && group.slotTypes[slot] == target.slotType
           && group.slotIndices[slot] == target.slotIndex
           && (group.slotFlags[slot] & state::build_data::scenarios::kSlotAuthFlag) != 0;
}

/** @return True when the target carries one exact supported type/schema pair. */
[[nodiscard]] bool supported_target(const ScriptableTarget& target) noexcept {
    const bool generatedStateLocal = target.stateLocalRoster && target.stateLocalRegion >= 0
                                     && target.rosterGroupIndex == kGeneratedRosterGroupIndex
                                     && target.sdkObjectIndex != kNoSdkObjectIndex;
    const bool canonical = !target.stateLocalRoster && target.stateLocalRegion < 0
                           && target.rosterGroupIndex != kGeneratedRosterGroupIndex
                           && target.sdkObjectIndex == kNoSdkObjectIndex;
    return target.slotType <= scene::kMaximumSlotType
           && target.slotIndex <= scene::kMaximumSlotIndex && target.authSchema != 0
           && (generatedStateLocal || canonical);
}

/** @return True when the bit count and the body agree to within one trailing byte. */
[[nodiscard]] bool valid_auth_storage(std::span<const std::byte> body,
                                      std::size_t bitCount) noexcept {
    if (body.empty() || bitCount > body.size() * 8U || bitCount + 7U < body.size() * 8U) {
        return false;
    }
    const std::size_t trailingBits = bitCount % 8U;
    if (trailingBits == 0) {
        return true;
    }
    const std::uint8_t paddingMask =
        static_cast<std::uint8_t>((std::uint16_t{1} << (8U - trailingBits)) - 1U);
    return (std::to_integer<std::uint8_t>(body.back()) & paddingMask) == 0;
}

/** Finds one committed full-slot guard while the runtime lock is held. */
[[nodiscard]] ScriptableGuard* find_guard(Instance& instance,
                                          const ScriptableTarget& target) noexcept {
    for (ScriptableGuard& guard : instance.scriptableGuards) {
        if (guard.occupied && same_target(guard.target, target)) {
            return &guard;
        }
    }
    return nullptr;
}

/** Finds unused storage for a full-slot guard without mutating it. */
[[nodiscard]] ScriptableGuard* free_guard(Instance& instance) noexcept {
    for (ScriptableGuard& guard : instance.scriptableGuards) {
        if (!guard.occupied) {
            return &guard;
        }
    }
    return nullptr;
}

/** @return True when a transport acknowledgement names the retained body byte-for-byte. */
[[nodiscard]] bool same_pending(const PendingScriptableOverride& left,
                                const PendingScriptableOverride& right) noexcept {
    return left.revision == right.revision && left.kind == right.kind
           && same_target(left.target, right.target) && left.generation == right.generation
           && left.expectedActivityClientGeneration == right.expectedActivityClientGeneration
           && left.sequence == right.sequence && left.dialogueSequence == right.dialogueSequence
           && left.dialogueCue == right.dialogueCue && left.bitCount == right.bitCount
           && left.byteCount == right.byteCount && left.channel == right.channel
           && left.lifetimeState == right.lifetimeState && left.body == right.body
           && left.sdkCompiled == right.sdkCompiled
           && (!left.target.stateLocalRoster
               || same_group(left.stateLocalRosterGroup, right.stateLocalRosterGroup));
}

/** Replaces one delivered full-ClientRef body, or appends its first value. */
[[nodiscard]] bool retain_scriptable_auth(Instance& instance,
                                          const PendingScriptableOverride& pending,
                                          std::uint64_t sourceGeneration) noexcept {
    if (pending.kind == ScriptableOverrideKind::lifetime) {
        return true;
    }
    if (pending.byteCount == 0 || pending.byteCount > pending.body.size()) {
        return false;
    }
    // Record the exact transported Ember ship body, including path revision and manifest.
    // Four fixed targets and short bodies keep this diagnostic bounded during flight.
    if (pending.target.registryKey == 0xF6FFB59EU && pending.target.slotType == 2
        && (pending.target.slotIndex == 22 || pending.target.slotIndex == 28
            || pending.target.slotIndex == 30 || pending.target.slotIndex == 32)
        && pending.byteCount <= 128) {
        std::array<char,257> hex{};
        constexpr char digits[] = "0123456789abcdef";
        for (std::size_t i=0;i<pending.byteCount;++i) {
            const auto byte=std::to_integer<unsigned>(pending.body[i]);
            hex[i*2]=digits[byte>>4]; hex[i*2+1]=digits[byte&15];
        }
        core::log::writef(core::log::Channel::server,core::log::Level::info,
            "ev=ember_transport stage=auth_staged slot=%u bits=%u body=%s",
            static_cast<unsigned>(pending.target.slotIndex),static_cast<unsigned>(pending.bitCount),hex.data());
    }
    PendingScriptableOverride owned = pending;
    if (owned.expectedActivityClientGeneration == 0) {
        owned.expectedActivityClientGeneration = sourceGeneration;
    }
    for (PendingScriptableOverride& retained : instance.scriptableAuthEstate) {
        if (same_client_ref(retained.target, pending.target)) {
            retained = owned;
            return true;
        }
    }
    try {
        instance.scriptableAuthEstate.push_back(owned);
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

/** @return True when this kind carries no push-side behaviour beyond its retained body. */
[[nodiscard]] bool tail_eligible(ScriptableOverrideKind kind) noexcept {
    return kind != ScriptableOverrideKind::lifetime && kind != ScriptableOverrideKind::squad;
}

/** @return True when this instance already holds a committed body for the same ClientRef. */
[[nodiscard]] bool pending_holds_target(const Instance& instance,
                                        const ScriptableTarget& target) noexcept {
    if (instance.view.outputPending && same_client_ref(instance.pendingScriptable.target, target)) {
        return true;
    }
    for (std::size_t index = 0; index < instance.pendingScriptableTailCount; ++index) {
        if (same_client_ref(instance.pendingScriptableTail[index].target, target)) {
            return true;
        }
    }
    return false;
}

/** @return True when another body may commit before the pending push carries the head out. */
[[nodiscard]] bool tail_has_room(const Instance& instance,
                                 const ScriptableRequest& request) noexcept {
    return instance.view.outputPending && tail_eligible(request.kind)
           && instance.view.outputKind == OutputKind::scriptableOverride
           && tail_eligible(instance.pendingScriptable.kind)
           && instance.pendingScriptableTailCount < instance.pendingScriptableTail.size()
           && !pending_holds_target(instance, request.target);
}

/** Queues one validated scriptable request in the shared ordered control lane. */
[[nodiscard]] bool enqueue_request(ScriptableRequest request,
                                   const ScriptableOutputReservation* reservation) noexcept {
    // A lifetime request changes activity state, so it carries no ClientRef slot to validate.
    const bool untargeted = request.kind == ScriptableOverrideKind::lifetime;
    if ((!untargeted && !supported_target(request.target))
        || !state::activity::binding_matches(request.binding)) {
        return false;
    }
    if (reservation != nullptr && !same_binding(reservation->binding, request.binding)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(request.binding);
    const bool ownsReservation =
        reservation != nullptr && instance != nullptr && instance->view.active
        && instance->view.scriptableReservationPending
        && same_reservation(instance->scriptableReservation, *reservation)
        && instance->view.scriptableRevision != (std::numeric_limits<std::uint64_t>::max)()
        && instance->view.scriptableRevision + 1 == reservation->revision;
    const bool burst = request.burstMember && request.expectedRevision != 0
                       && tail_eligible(request.kind) && instance != nullptr;
    if ((!burst && has_queued_control(request.binding))
        || (!burst && instance != nullptr && instance->view.outputPending)
        || (!burst && reservation != nullptr && !ownsReservation)
        || (!burst && reservation == nullptr && instance != nullptr
            && instance->view.scriptableReservationPending)) {
        ++g_refusedControls;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    if (reservation != nullptr) {
        request.expectedRevision = reservation->revision;
        request.expectedIntentSequence = reservation->intentSequence;
    }
    PendingInput pending{};
    pending.kind = PendingKind::scriptableControl;
    pending.scriptableControl = request;
    if (!append_pending(std::move(pending))) {
        ++g_refusedControls;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    ++g_queuedControls;
    if (reservation != nullptr) {
        clear_reservation(*instance);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

/** Clears one exact pending body while the runtime lock is held. */
void cancel_pending(Instance& instance,
                    const state::activity::SessionBinding& binding,
                    std::uint64_t expectedRevision) noexcept {
    const std::uint64_t now = GetTickCount64();
    Event event{};
    event.binding = binding;
    event.tick = now;
    event.kind = EventKind::scriptableOverrideCanceled;
    for (std::size_t index = 0; index < instance.pendingScriptableTailCount; ++index) {
        event.scriptableRevision = instance.pendingScriptableTail[index].revision;
        append_event(event);
    }
    instance.pendingScriptableTail.fill({});
    instance.pendingScriptableTailCount = 0;
    instance.pendingScriptable = {};
    instance.view.outputPending = false;
    instance.view.outputKind = OutputKind::none;
    instance.view.outputStatus = OutputStatus::canceled;
    event.scriptableRevision = expectedRevision;
    append_event(event);
    instance.view.lastEventSequence = g_sequence;
}

} // namespace

namespace detail {

/** Assigns and encodes one typed counter without committing it before transport staging. */
void apply_scriptable_control(const ScriptableRequest& request, std::uint64_t now) noexcept {
    Event event{};
    event.binding = request.binding;
    event.tick = now;
    event.kind = EventKind::operatorRefused;
    Instance* const instance = find_instance(request.binding);
    const bool durableAssignment =
        request.expectedIntentSequence == 0
        || state::activity::mission::intent_output_assigned(
            request.binding, request.expectedIntentSequence, request.expectedRevision);
    const bool joinsTail = instance != nullptr && request.burstMember
                           && tail_has_room(*instance, request)
                           && request.expectedRevision == instance->view.scriptableRevision;
    if (instance == nullptr || !instance->view.active
        || (instance->view.outputPending && !joinsTail) || !durableAssignment
        || instance->view.scriptableRevision == (std::numeric_limits<std::uint64_t>::max)()
        || (request.expectedRevision != 0 && !joinsTail
            && instance->view.scriptableRevision + 1 != request.expectedRevision)) {
        ++g_refusedControls;
        append_event(event);
        if (instance != nullptr) {
            instance->view.lastEventSequence = g_sequence;
        }
        return;
    }

    // A lifetime request owns no ClientRef slot, so it takes no full-slot counter guard.
    const bool untargeted = request.kind == ScriptableOverrideKind::lifetime;
    ScriptableGuard* guard = untargeted ? nullptr : find_guard(*instance, request.target);
    ScriptableGuard candidate{};
    if (!untargeted) {
        if (guard == nullptr) {
            guard = free_guard(*instance);
            candidate.target = request.target;
            candidate.occupied = true;
        } else {
            candidate = *guard;
        }
    }
    PendingScriptableOverride pending{};
    pending.target = request.target;
    pending.stateLocalRosterGroup = request.stateLocalRosterGroup;
    pending.revision = request.expectedRevision == 0 ? instance->view.scriptableRevision + 1
                                                     : request.expectedRevision;
    pending.kind = request.kind;
    pending.expectedActivityClientGeneration = request.expectedActivityClientGeneration;
    std::size_t written = 0;
    std::size_t writtenBits = 0;
    bool encoded = untargeted || guard != nullptr;
    if (encoded && request.kind == ScriptableOverrideKind::lifetime) {
        pending.lifetimeState = request.lifetimeState;
    } else if (encoded && request.kind == ScriptableOverrideKind::squad) {
        std::uint32_t generation = 0;
        encoded = request.requestedCountLength <= request.requestedCounts.size();
        if (encoded) {
            const std::span<const std::int32_t> counts(request.requestedCounts.data(),
                                                       request.requestedCountLength);
            encoded = squad::next_generation(candidate.squad, generation)
                      && squad::encode({counts,
                                        generation,
                                        request.squadMode,
                                        request.nameHash,
                                        request.squadAuthoredProfile},
                                       candidate.squad,
                                       pending.body,
                                       written,
                                       writtenBits);
        }
        pending.generation = generation;
        if (writtenBits <= (std::numeric_limits<std::uint16_t>::max)()) {
            pending.bitCount = static_cast<std::uint16_t>(writtenBits);
        } else {
            encoded = false;
        }
    } else if (encoded && request.kind == ScriptableOverrideKind::combatantChannel) {
        std::uint32_t revision = 0;
        encoded = auth::next_type2_revision(candidate.type2, revision);
        candidate.type2.revision = revision;
        encoded =
            encoded && auth::set_type2_channel(candidate.type2, request.channelHash, request.value)
            && auth::encode_type2_channels(candidate.type2, pending.body, written, writtenBits);
        if (writtenBits <= (std::numeric_limits<std::uint16_t>::max)()) {
            pending.bitCount = static_cast<std::uint16_t>(writtenBits);
        } else {
            encoded = false;
        }
        pending.channelHash = request.channelHash;
        pending.channelValue = request.value;
        pending.generation = revision;
    } else if (encoded && request.kind == ScriptableOverrideKind::combatantBinding) {
        std::uint32_t revision = 0;
        encoded = auth::next_type2_revision(candidate.type2, revision);
        candidate.type2.revision = revision;
        candidate.type2.actorBinding = auth::Type2ActorBinding::squadMember;
        encoded =
            encoded
            && auth::encode_type2_channels(candidate.type2, pending.body, written, writtenBits);
        if (writtenBits <= (std::numeric_limits<std::uint16_t>::max)()) {
            pending.bitCount = static_cast<std::uint16_t>(writtenBits);
        } else {
            encoded = false;
        }
        pending.generation = revision;
    } else if (encoded && request.kind == ScriptableOverrideKind::object) {
        pending.bitCount = static_cast<std::uint16_t>(auth::kType4BitCount);
        std::int32_t generation = 0;
        encoded = auth::next_type4_generation(candidate.type4, generation)
                  && auth::encode_type4({generation, request.entryIndex, request.active},
                                        candidate.type4,
                                        pending.body,
                                        written);
        pending.generation = static_cast<std::uint64_t>(generation);
    } else if (encoded && request.kind == ScriptableOverrideKind::sequence) {
        pending.bitCount = static_cast<std::uint16_t>(auth::kType5BitCount);
        std::uint8_t revision = 0;
        encoded = auth::next_type5_revision(candidate.type5, revision)
                  && auth::encode_type5({revision}, candidate.type5, pending.body, written);
        pending.generation = revision;
    } else if (encoded && request.kind == ScriptableOverrideKind::cinematic) {
        pending.bitCount = static_cast<std::uint16_t>(auth::kType6BitCount);
        std::uint32_t generation = 0;
        encoded = auth::next_type6_generation(candidate.type6, generation)
                  && auth::encode_type6(
                      {generation, request.active}, candidate.type6, pending.body, written);
        pending.generation = generation;
    } else if (encoded && request.kind == ScriptableOverrideKind::performance) {
        pending.bitCount = static_cast<std::uint16_t>(auth::kType42BitCount);
        std::int32_t generation = 0;
        auth::Type42Preset preset{};
        preset.nameHash = request.nameHash.value_or(0);
        encoded = auth::next_type42_generation(candidate.type42, generation);
        preset.generation = generation;
        encoded = encoded && auth::encode_type42(preset, candidate.type42, pending.body, written);
        pending.generation = static_cast<std::uint64_t>(generation);
    } else if (encoded && request.kind == ScriptableOverrideKind::type23) {
        pending.channel = request.channel;
        pending.bitCount = static_cast<std::uint16_t>(auth::kType23BitCount);
        auth::Type23Body body{};
        auth::Type23SequenceGuard composedGuard = candidate.type23;
        for (const PendingScriptableOverride& retained : instance->scriptableAuthEstate) {
            if (!same_client_ref(retained.target, request.target)
                || retained.bitCount != auth::kType23BitCount
                || retained.byteCount != auth::kType23ByteCount
                || !auth::decode_type23_body(std::span(retained.body).first(retained.byteCount),
                                             body)) {
                continue;
            }
            for (std::size_t index = 0; index < body.channels.size(); ++index) {
                composedGuard.last[index] =
                    (std::max)(composedGuard.last[index], body.channels[index].sequence);
            }
            break;
        }
        std::int16_t sequence = 0;
        encoded = auth::next_type23_sequence(composedGuard, request.channel, sequence);
        if (encoded) {
            const std::size_t channel = static_cast<std::size_t>(request.channel);
            body.channels[channel] = {request.value, sequence, request.snap};
            encoded = auth::encode_type23_body(body, pending.body, written);
        }
        pending.sequence = sequence;
    } else if (encoded && request.kind == ScriptableOverrideKind::type31) {
        pending.bitCount = static_cast<std::uint16_t>(auth::kType31BitCount);
        std::uint64_t generation = 0;
        encoded = auth::next_type31_generation(candidate.type31, generation)
                  && auth::encode_type31({generation}, candidate.type31, pending.body, written);
        pending.generation = generation;
    } else if (encoded && request.kind == ScriptableOverrideKind::objectiveReset) {
        pending.bitCount = static_cast<std::uint16_t>(auth::kType3BitCount);
        std::int32_t generation = 0;
        auth::Type3Body body{};
        encoded = auth::next_type3_generation(candidate.type3, generation);
        body.generation = generation;
        encoded = encoded && auth::encode_type3(body, candidate.type3, pending.body, written);
        pending.generation = static_cast<std::uint64_t>(generation);
    } else if (encoded && request.kind == ScriptableOverrideKind::task) {
        pending.bitCount = static_cast<std::uint16_t>(auth::kType38BitCount);
        std::int32_t generation = 0;
        encoded = auth::next_type38_generation(candidate.type38, generation)
                  && auth::encode_type38({generation}, candidate.type38, pending.body, written);
        pending.generation = static_cast<std::uint64_t>(generation);
    } else if (encoded && request.kind == ScriptableOverrideKind::authoredScene) {
        pending.bitCount = scene::kAuthoredSceneAuthBitCount;
        std::uint32_t generation = 0;
        encoded = next_authored_scene_generation(candidate.authoredSceneGeneration, generation)
                  && encode_authored_scene(generation, pending.body, written);
        pending.generation = generation;
    } else if (encoded && request.kind == ScriptableOverrideKind::dialogue) {
        pending.bitCount = static_cast<std::uint16_t>(auth::kType53BitCount);
        pending.dialogueCue = request.dialogueCue;
        std::int32_t sequence = 0;
        encoded = auth::next_type53_sequence(candidate.type53, request.dialogueCue, sequence)
                  && auth::encode_type53(
                      {request.dialogueCue, sequence}, candidate.type53, pending.body, written);
        pending.dialogueSequence = sequence;
    } else if (encoded && request.kind == ScriptableOverrideKind::sdkAuth) {
        written = request.authByteCount;
        pending.bitCount = request.authBitCount;
        pending.sdkCompiled = true;
        std::copy_n(request.authBody.begin(), written, pending.body.begin());
    } else {
        encoded = false;
    }
    // These mission APIs emit root-field patches; native overrides are complete Auth
    // replacements. Keep the last transported state before publishing another command.
    namespace message = middleware::bap::activity_message;
    const auto patch = std::span(pending.body).first(written);
    const bool missionPatch = encoded && (request.kind == ScriptableOverrideKind::squad
        || (request.kind == ScriptableOverrideKind::sdkAuth
            && ((request.target.slotType == 1 && request.target.authSchema == squad::kSchema
                 && message::squad_objective::validate(patch, pending.bitCount))
                || (request.target.slotType == 2 && request.target.authSchema == 0x80807DA1U
                    && (message::combatant_path::validate(patch, pending.bitCount)
                        || message::combatant_delivery::validate(patch, pending.bitCount)
                        || message::combatant_action::validate(patch, pending.bitCount)
                        || message::combatant_retire::validate(patch, pending.bitCount))))));
    if (missionPatch) {
        std::span<const std::byte> previous{};
        std::size_t previousBits=0;
        // A tail cannot contain the same ClientRef as another pending body, so its
        // predecessor is always in the transported estate, not an uncommitted row.
        for (const auto& retained : instance->scriptableAuthEstate) {
            if (same_client_ref(retained.target, request.target)) {
                previous=std::span(retained.body).first(retained.byteCount);
                previousBits=retained.bitCount;
                break;
            }
        }
        std::size_t composedBits{};
        encoded=message::mission_auth_patch::compose(request.target.authSchema,
            previous,previousBits,patch,pending.bitCount,pending.body,written,composedBits);
        if (encoded) pending.bitCount=static_cast<std::uint16_t>(composedBits);
    }
    if (!encoded || written > (std::numeric_limits<std::uint16_t>::max)()) {
        ++g_refusedControls;
    } else {
        if (guard != nullptr && !guard->occupied) {
            // Reserve only the target identity. Mutable lane state commits after transport stages
            // the exact body; type 2 in particular retains a complete per-actor channel set.
            guard->target = request.target;
            guard->occupied = true;
        }
        pending.byteCount = static_cast<std::uint16_t>(written);
        touch(*instance);
        if (joinsTail) {
            instance->pendingScriptableTail[instance->pendingScriptableTailCount] = pending;
            ++instance->pendingScriptableTailCount;
        } else {
            instance->pendingScriptable = pending;
            instance->view.outputPending = true;
            instance->view.outputKind = OutputKind::scriptableOverride;
            instance->view.outputStatus = OutputStatus::pending;
            instance->view.lastOutputAttemptTick = 0;
            instance->view.lastOutputSourceGeneration = 0;
            instance->view.outputAttempts = 0;
        }
        if (!joinsTail) {
            instance->view.scriptableRevision = pending.revision;
        }
        event.kind = EventKind::scriptableOverrideCommitted;
        event.scriptableRevision = pending.revision;
    }
    append_event(event);
    instance->view.lastEventSequence = g_sequence;
}

} // namespace detail

/** Holds one unarmed exact revision while its durable Mission State assignment publishes. */
bool reserve_scriptable_output(const state::activity::SessionBinding& binding,
                               ScriptableOutputReservation& output,
                               std::uint64_t intentSequence) noexcept {
    output = {};
    if (!state::activity::binding_matches(binding)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    const bool available =
        instance != nullptr && instance->view.active && !instance->view.outputPending
        && !instance->view.scriptableReservationPending && !has_queued_control(binding)
        && instance->view.scriptableRevision != (std::numeric_limits<std::uint64_t>::max)()
        && g_scriptableReservationSequence != (std::numeric_limits<std::uint64_t>::max)();
    if (!available) {
        ++g_refusedControls;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    ++g_scriptableReservationSequence;
    output.binding = binding;
    output.resetGeneration = g_scriptableReservationGeneration;
    output.token = g_scriptableReservationSequence;
    output.revision = instance->view.scriptableRevision + 1;
    output.intentSequence = intentSequence;
    instance->scriptableReservation = output;
    instance->view.scriptableReservedRevision = output.revision;
    instance->view.scriptableReservationPending = true;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

/** Releases one exact unarmed reservation without changing the Host output revision. */
bool release_scriptable_output(const ScriptableOutputReservation& reservation) noexcept {
    if (reservation.resetGeneration == 0 || reservation.token == 0 || reservation.revision == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(reservation.binding);
    const bool owned = instance != nullptr && instance->view.scriptableReservationPending
                       && same_reservation(instance->scriptableReservation, reservation);
    if (owned) {
        clear_reservation(*instance);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return owned;
}

/** Atomically withdraws one queued durable reducer row or reports its committed disposition. */
ScriptableWithdrawStatus withdraw_scriptable_output(const state::activity::SessionBinding& binding,
                                                    std::uint64_t intentSequence,
                                                    std::uint64_t expectedRevision) noexcept {
    if (intentSequence == 0 || expectedRevision == 0) {
        return ScriptableWithdrawStatus::mismatch;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    if (instance == nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        return ScriptableWithdrawStatus::mismatch;
    }
    if (instance->view.scriptableTransportRevision == expectedRevision) {
        ReleaseSRWLockExclusive(&g_lock);
        return ScriptableWithdrawStatus::transportStaged;
    }
    if (instance->view.scriptableRevision > expectedRevision
        || instance->view.scriptableTransportRevision > expectedRevision) {
        ReleaseSRWLockExclusive(&g_lock);
        return ScriptableWithdrawStatus::advanced;
    }
    if (instance->view.scriptableRevision == expectedRevision) {
        const ScriptableWithdrawStatus status =
            instance->view.outputPending
                    && instance->view.outputKind == OutputKind::scriptableOverride
                ? ScriptableWithdrawStatus::committed
                : ScriptableWithdrawStatus::canceled;
        ReleaseSRWLockExclusive(&g_lock);
        return status;
    }
    if (instance->view.outputPending || instance->view.scriptableReservationPending) {
        ReleaseSRWLockExclusive(&g_lock);
        return ScriptableWithdrawStatus::mismatch;
    }
    for (std::size_t index = g_pendingRead; index < g_pending.size(); ++index) {
        PendingInput& pending = g_pending[index];
        if (pending.kind != PendingKind::scriptableControl
            || !same_binding(pending.scriptableControl.binding, binding)
            || pending.scriptableControl.expectedIntentSequence != intentSequence
            || pending.scriptableControl.expectedRevision != expectedRevision) {
            continue;
        }
        pending.scriptableControl = {};
        pending.kind = PendingKind::discardedControl;
        --g_queuedControls;
        ReleaseSRWLockExclusive(&g_lock);
        return ScriptableWithdrawStatus::withdrawn;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return ScriptableWithdrawStatus::absent;
}

/** Queues one generation-bound type-23 update for an exact package-derived ClientRef. */
bool request_type23_override(const state::activity::SessionBinding& binding,
                             const ScriptableTarget& target,
                             auth::Type23Channel channel,
                             float value,
                             bool snap,
                             std::uint64_t expectedActivityClientGeneration,
                             const ScriptableOutputReservation* reservation) noexcept {
    const auto channelIndex = static_cast<std::size_t>(channel);
    if (target.slotType != auth::kType23SlotType || target.authSchema != auth::kType23Schema
        || channelIndex >= auth::kType23ChannelCount || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.channel = channel;
    request.value = value;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::type23;
    request.snap = snap;
    return enqueue_request(request, reservation);
}

/** Queues one package-owned type-4 entry transition. */
bool request_state_local_type4_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    std::int32_t entryIndex,
    bool active,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation,
    const ScriptableOutputReservation* burstHead) noexcept {
    if (target.slotType != auth::kType4SlotType || target.authSchema != auth::kType4Schema
        || !valid_state_local_group(target, stateLocalRosterGroup) || entryIndex < 0
        || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.entryIndex = entryIndex;
    request.active = active;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::object;
    if (burstHead != nullptr) {
        // Every body in one burst answers under the revision the head reserved for all of them.
        request.burstMember = true;
        request.expectedRevision = burstHead->revision;
        request.expectedIntentSequence = burstHead->intentSequence;
    }
    return enqueue_request(request, burstHead != nullptr ? nullptr : reservation);
}

/** Queues one retained named actor-channel write. */
bool request_state_local_type2_channel_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    std::uint32_t channelHash,
    float value,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != auth::kType2SlotType || target.authSchema != auth::kType2Schema
        || !valid_state_local_group(target, stateLocalRosterGroup) || !std::isfinite(value)
        || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.channelHash = channelHash;
    request.value = value;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::combatantChannel;
    return enqueue_request(request, reservation);
}

/** Queues squad-member binding for one exact generated type-2 combatant. */
bool request_state_local_type2_squad_binding(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != auth::kType2SlotType || target.authSchema != auth::kType2Schema
        || !valid_state_local_group(target, stateLocalRosterGroup)
        || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::combatantBinding;
    return enqueue_request(request, reservation);
}

/** Queues one generation-bound type-23 update from an exact generated roster group. */
bool request_state_local_type23_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    auth::Type23Channel channel,
    float value,
    bool snap,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    const auto channelIndex = static_cast<std::size_t>(channel);
    if (target.slotType != auth::kType23SlotType || target.authSchema != auth::kType23Schema
        || !valid_state_local_group(target, stateLocalRosterGroup)
        || channelIndex >= auth::kType23ChannelCount || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.channel = channel;
    request.value = value;
    request.kind = ScriptableOverrideKind::type23;
    request.snap = snap;
    return enqueue_request(request, reservation);
}

/** Queues one type-31 pulse for an exact package-derived ClientRef. */
bool request_type31_override(const state::activity::SessionBinding& binding,
                             const ScriptableTarget& target,
                             const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != auth::kType31SlotType || target.authSchema != auth::kType31Schema) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.kind = ScriptableOverrideKind::type31;
    return enqueue_request(request, reservation);
}

/** Queues one generation-bound type-31 pulse from an exact generated roster group. */
bool request_state_local_type31_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != auth::kType31SlotType || target.authSchema != auth::kType31Schema
        || !valid_state_local_group(target, stateLocalRosterGroup)
        || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::type31;
    return enqueue_request(request, reservation);
}

/** Queues one state-local sequence override, but only for an exact type-5 target. */
bool request_state_local_sequence_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != auth::kType5SlotType || target.authSchema != auth::kType5Schema
        || !valid_state_local_group(target, stateLocalRosterGroup)
        || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::sequence;
    return enqueue_request(request, reservation);
}

/** Queues one state-local cinematic override, but only for an exact type-5 target. */
bool request_state_local_cinematic_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    bool active,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != auth::kType6SlotType || target.authSchema != auth::kType6Schema
        || !valid_state_local_group(target, stateLocalRosterGroup)
        || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::cinematic;
    request.active = active;
    return enqueue_request(request, reservation);
}

/** Queues one type-42 performance start; the encoder assigns the rising generation. */
bool request_state_local_performance_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    std::uint32_t stateNameHash,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != auth::kType42SlotType || target.authSchema != auth::kType42Schema
        || !valid_state_local_group(target, stateLocalRosterGroup)
        || expectedActivityClientGeneration == 0 || stateNameHash == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::performance;
    request.nameHash = stateNameHash;
    return enqueue_request(request, reservation);
}

/** Queues one objective reset from an exact generated roster group. */
bool request_state_local_objective_reset(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != auth::kType3SlotType || target.authSchema != auth::kType3Schema
        || !valid_state_local_group(target, stateLocalRosterGroup)
        || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::objectiveReset;
    return enqueue_request(request, reservation);
}

/** Queues one generation-bound authored-task change from an exact generated roster group. */
bool request_state_local_task_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != auth::kType38SlotType || target.authSchema != auth::kType38Schema
        || !valid_state_local_group(target, stateLocalRosterGroup)
        || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::task;
    return enqueue_request(request, reservation);
}

/** Queues one generation-bound authored-scene activation from an exact generated roster group. */
bool request_state_local_authored_scene_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != scene::kAuthoredSceneSlotType
        || target.authSchema != scene::kAuthoredSceneAuthSchema
        || !valid_state_local_group(target, stateLocalRosterGroup)
        || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::authoredScene;
    return enqueue_request(request, reservation);
}

/** Queues one SDK-bounded dialogue line pulse from an exact generated roster group. */
bool request_state_local_dialogue_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup& stateLocalRosterGroup,
    std::uint16_t cueIndex,
    std::uint16_t authoredCueCount,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (target.slotType != auth::kType53SlotType || target.authSchema != auth::kType53Schema
        || !valid_state_local_group(target, stateLocalRosterGroup) || authoredCueCount == 0
        || authoredCueCount > auth::kType53EntryCount || cueIndex >= authoredCueCount
        || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    request.stateLocalRosterGroup = stateLocalRosterGroup;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.dialogueCue = cueIndex;
    request.kind = ScriptableOverrideKind::dialogue;
    return enqueue_request(request, reservation);
}

/** Queues one squad placement intent for an exact package-derived ClientRef. */
bool request_squad_override(const state::activity::SessionBinding& binding,
                            const ScriptableTarget& target,
                            const state::build_data::scenarios::RosterGroup* stateLocalRosterGroup,
                            std::span<const std::int32_t> requestedCounts,
                            squad::Mode mode,
                            std::uint64_t expectedActivityClientGeneration,
                            std::optional<std::uint32_t> nameHash,
                            const ScriptableOutputReservation* reservation,
                            std::array<std::int8_t, 4> authoredProfile) noexcept {
    if (target.slotType != squad::kSlotType || target.authSchema != squad::kSchema
        || (target.stateLocalRoster
                ? stateLocalRosterGroup == nullptr
                      || !valid_state_local_group(target, *stateLocalRosterGroup)
                : stateLocalRosterGroup != nullptr)
        || requestedCounts.size() < squad::kMinimumRequestedCountLength
        || requestedCounts.size() > squad::kMaximumRequestedCountLength
        || expectedActivityClientGeneration == 0
        || !squad::valid_mode(mode)
        || !std::ranges::all_of(requestedCounts, [](std::int32_t count) { return count >= 0; })) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    if (stateLocalRosterGroup != nullptr) {
        request.stateLocalRosterGroup = *stateLocalRosterGroup;
    }
    std::ranges::copy(requestedCounts, request.requestedCounts.begin());
    request.requestedCountLength = requestedCounts.size();
    request.squadAuthoredProfile = authoredProfile;
    request.nameHash = nameHash;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.squadMode = mode;
    request.kind = ScriptableOverrideKind::squad;
    return enqueue_request(request, reservation);
}

/** Queues one generation-bound activity lifetime state through the serialized output slot. */
bool request_lifetime_override(const state::activity::SessionBinding& binding,
                               std::uint8_t lifetimeState,
                               std::uint64_t expectedActivityClientGeneration,
                               const ScriptableOutputReservation* reservation) noexcept {
    // Above the highest jump-table entry the client's spawn gate jumps out of its image.
    if (lifetimeState > kMaximumLifetimeState || expectedActivityClientGeneration == 0) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::lifetime;
    request.lifetimeState = lifetimeState;
    return enqueue_request(request, reservation);
}

/** Queues one exact SDK-compiled Auth body for a generation-bound ClientRef. */
bool request_sdk_auth_override(
    const state::activity::SessionBinding& binding,
    const ScriptableTarget& target,
    const state::build_data::scenarios::RosterGroup* stateLocalRosterGroup,
    std::span<const std::byte> body,
    std::uint16_t bitCount,
    std::uint64_t expectedActivityClientGeneration,
    const ScriptableOutputReservation* reservation) noexcept {
    if (body.empty() || body.size() > scene::kAuthOverrideByteCapacity
        || body.size() > (std::numeric_limits<std::uint16_t>::max)()
        || expectedActivityClientGeneration == 0 || !valid_auth_storage(body, bitCount)
        || (target.stateLocalRoster
                ? stateLocalRosterGroup == nullptr
                      || !valid_state_local_group(target, *stateLocalRosterGroup)
                : stateLocalRosterGroup != nullptr)) {
        return false;
    }
    ScriptableRequest request{};
    request.binding = binding;
    request.target = target;
    if (stateLocalRosterGroup != nullptr) {
        request.stateLocalRosterGroup = *stateLocalRosterGroup;
    }
    std::copy(body.begin(), body.end(), request.authBody.begin());
    request.authBitCount = bitCount;
    request.authByteCount = static_cast<std::uint16_t>(body.size());
    request.expectedActivityClientGeneration = expectedActivityClientGeneration;
    request.kind = ScriptableOverrideKind::sdkAuth;
    return enqueue_request(request, reservation);
}

/** Reads the one pending typed ClientRef body without changing its counter. */
bool pending_scriptable_override(const state::activity::SessionBinding& binding,
                                 PendingScriptableOverride& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    const Instance* const instance = find_instance(binding);
    const bool pending = instance != nullptr && instance->view.active
                         && instance->view.outputPending
                         && instance->view.outputKind == OutputKind::scriptableOverride
                         && instance->pendingScriptable.revision != 0;
    if (pending) {
        output = instance->pendingScriptable;
    }
    ReleaseSRWLockShared(&g_lock);
    return pending;
}

/** Reads one pending body only for the ActivityClient generation that authorized it. */
bool pending_scriptable_override_for_activity_client(const state::activity::SessionBinding& binding,
                                                     std::uint64_t activityClientGeneration,
                                                     PendingScriptableOverride& output) noexcept {
    output = {};
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    bool pending = instance != nullptr && instance->view.active && instance->view.outputPending
                   && instance->view.outputKind == OutputKind::scriptableOverride
                   && instance->pendingScriptable.revision != 0;
    if (pending && instance->pendingScriptable.expectedActivityClientGeneration != 0
        && instance->pendingScriptable.expectedActivityClientGeneration
               != activityClientGeneration) {
        const std::uint64_t revision = instance->pendingScriptable.revision;
        cancel_pending(*instance, binding, revision);
        pending = false;
    }
    if (pending) {
        output = instance->pendingScriptable;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return pending;
}

/** Cancels one exact unstaged typed override revision without advancing its slot counter. */
bool cancel_pending_scriptable_override(const state::activity::SessionBinding& binding,
                                        std::uint64_t expectedRevision) noexcept {
    if (expectedRevision == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    const bool canceled = instance != nullptr && instance->view.active
                          && instance->view.outputPending
                          && instance->view.outputKind == OutputKind::scriptableOverride
                          && instance->pendingScriptable.revision == expectedRevision;
    if (canceled) {
        cancel_pending(*instance, binding, expectedRevision);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return canceled;
}

/** Records one refused typed-body attempt without consuming its sequence or generation. */
void note_scriptable_attempt(const state::activity::SessionBinding& binding,
                             std::uint64_t sourceGeneration,
                             const PendingScriptableOverride& pending,
                             OutputStatus status) noexcept {
    if (pending.revision == 0 || status == OutputStatus::idle || status == OutputStatus::pending
        || status == OutputStatus::transportStaged || status == OutputStatus::canceled) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    if (instance != nullptr && instance->view.outputPending
        && instance->view.outputKind == OutputKind::scriptableOverride
        && same_pending(instance->pendingScriptable, pending)) {
        instance->view.lastOutputAttemptTick = GetTickCount64();
        instance->view.lastOutputSourceGeneration = sourceGeneration;
        ++instance->view.outputAttempts;
        instance->view.outputStatus = status;
        const bool terminal = status == OutputStatus::noLayout || status == OutputStatus::noGroups
                              || status == OutputStatus::noOverrideTarget
                              || status == OutputStatus::ambiguousLinks
                              || status == OutputStatus::frameRefused;
        if (terminal) {
            const std::uint64_t revision = instance->pendingScriptable.revision;
            cancel_pending(*instance, binding, revision);
            instance->view.outputStatus = status;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** @return True when this body carries the exact next counter its committed guard expects. */
[[nodiscard]] bool staged_counter_matches(const ScriptableGuard* guard,
                                          const PendingScriptableOverride& pending) noexcept {
    bool nextCounter = false;
    if (pending.kind == ScriptableOverrideKind::lifetime) {
        nextCounter = true;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::squad
               && pending.generation <= squad::kMaximumGeneration) {
        std::uint32_t next = 0;
        nextCounter = squad::next_generation(guard->squad, next) && next == pending.generation;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::combatantChannel) {
        auth::Type2ChannelState candidate = guard->type2;
        std::uint32_t revision = 0;
        nextCounter =
            auth::next_type2_revision(candidate, revision) && revision == pending.generation;
        candidate.revision = revision;
        nextCounter =
            nextCounter
            && auth::set_type2_channel(candidate, pending.channelHash, pending.channelValue);
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::combatantBinding) {
        auth::Type2ChannelState candidate = guard->type2;
        std::uint32_t revision = 0;
        nextCounter =
            auth::next_type2_revision(candidate, revision) && revision == pending.generation;
        candidate.revision = revision;
        candidate.actorBinding = auth::Type2ActorBinding::squadMember;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::object) {
        std::int32_t next = 0;
        nextCounter = auth::next_type4_generation(guard->type4, next)
                      && static_cast<std::uint64_t>(next) == pending.generation;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::sequence) {
        std::uint8_t next = 0;
        nextCounter = auth::next_type5_revision(guard->type5, next) && next == pending.generation;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::cinematic) {
        std::uint32_t next = 0;
        nextCounter = auth::next_type6_generation(guard->type6, next) && next == pending.generation;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::performance) {
        std::int32_t next = 0;
        nextCounter = auth::next_type42_generation(guard->type42, next)
                      && static_cast<std::uint64_t>(next) == pending.generation;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::type23) {
        std::int16_t next = 0;
        nextCounter = auth::next_type23_sequence(guard->type23, pending.channel, next)
                      && next == pending.sequence;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::type31) {
        std::uint64_t next = 0;
        nextCounter =
            auth::next_type31_generation(guard->type31, next) && next == pending.generation;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::objectiveReset) {
        std::int32_t next = 0;
        nextCounter = auth::next_type3_generation(guard->type3, next)
                      && static_cast<std::uint64_t>(next) == pending.generation;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::task) {
        std::int32_t next = 0;
        nextCounter = auth::next_type38_generation(guard->type38, next)
                      && static_cast<std::uint64_t>(next) == pending.generation;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::authoredScene) {
        std::uint32_t next = 0;
        nextCounter = next_authored_scene_generation(guard->authoredSceneGeneration, next)
                      && next == pending.generation;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::dialogue) {
        std::int32_t next = 0;
        nextCounter = auth::next_type53_sequence(guard->type53, pending.dialogueCue, next)
                      && next == pending.dialogueSequence;
    } else if (guard != nullptr && pending.kind == ScriptableOverrideKind::sdkAuth
               && pending.sdkCompiled) {
        nextCounter = true;
    }
    return nextCounter;
}

/** Advances one committed guard to the body that has just reached transport. */
void advance_staged_guard(ScriptableGuard* guard,
                          const PendingScriptableOverride& pending) noexcept {
    if (guard == nullptr) {
        return;
    }
    if (pending.kind == ScriptableOverrideKind::squad) {
        guard->squad.last = static_cast<std::uint32_t>(pending.generation);
        guard->squad.hasLast = true;
    } else if (pending.kind == ScriptableOverrideKind::combatantChannel) {
        guard->type2.revision = static_cast<std::uint32_t>(pending.generation);
        static_cast<void>(
            auth::set_type2_channel(guard->type2, pending.channelHash, pending.channelValue));
    } else if (pending.kind == ScriptableOverrideKind::combatantBinding) {
        guard->type2.revision = static_cast<std::uint32_t>(pending.generation);
        guard->type2.actorBinding = auth::Type2ActorBinding::squadMember;
    } else if (pending.kind == ScriptableOverrideKind::object) {
        guard->type4.last = static_cast<std::int32_t>(pending.generation);
        guard->type4.hasLast = true;
    } else if (pending.kind == ScriptableOverrideKind::sequence) {
        guard->type5.last = static_cast<std::uint8_t>(pending.generation);
        guard->type5.hasLast = true;
    } else if (pending.kind == ScriptableOverrideKind::cinematic) {
        guard->type6.last = static_cast<std::uint32_t>(pending.generation);
        guard->type6.hasLast = true;
    } else if (pending.kind == ScriptableOverrideKind::performance) {
        guard->type42.last = static_cast<std::int32_t>(pending.generation);
        guard->type42.hasLast = true;
    } else if (pending.kind == ScriptableOverrideKind::type23) {
        guard->type23.last[static_cast<std::size_t>(pending.channel)] = pending.sequence;
    } else if (pending.kind == ScriptableOverrideKind::type31) {
        guard->type31.last = pending.generation;
        guard->type31.hasLast = true;
    } else if (pending.kind == ScriptableOverrideKind::objectiveReset) {
        guard->type3.last = static_cast<std::int32_t>(pending.generation);
        guard->type3.hasLast = true;
    } else if (pending.kind == ScriptableOverrideKind::task) {
        guard->type38.last = static_cast<std::int32_t>(pending.generation);
        guard->type38.hasLast = true;
    } else if (pending.kind == ScriptableOverrideKind::authoredScene) {
        guard->authoredSceneGeneration = static_cast<std::uint32_t>(pending.generation);
    } else if (pending.kind == ScriptableOverrideKind::dialogue) {
    }
}

/** Records that one pending body reached the transport, so its retained estate can advance. */
void note_scriptable_transport_staged(const state::activity::SessionBinding& binding,
                                      std::uint64_t sourceGeneration,
                                      const PendingScriptableOverride& pending) noexcept {
    if (pending.revision == 0
        || (pending.expectedActivityClientGeneration != 0
            && pending.expectedActivityClientGeneration != sourceGeneration)) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    ScriptableGuard* guard = instance != nullptr ? find_guard(*instance, pending.target) : nullptr;
    const bool nextCounter = staged_counter_matches(guard, pending);
    if (instance != nullptr && nextCounter && instance->view.outputPending
        && instance->view.outputKind == OutputKind::scriptableOverride
        && same_pending(instance->pendingScriptable, pending)) {
        const bool retained = retain_scriptable_auth(*instance, pending, sourceGeneration);
        if (!retained) {
            ++g_refusedControls;
            ReleaseSRWLockExclusive(&g_lock);
            return;
        }
        advance_staged_guard(guard, pending);
        if (pending.kind == ScriptableOverrideKind::lifetime) {
            // Latch the state so every later msg 5 keeps reporting it.
            instance->view.lifetimeState = pending.lifetimeState;
        }
        instance->view.lastOutputAttemptTick = GetTickCount64();
        Event event{};
        event.binding = binding;
        event.tick = instance->view.lastOutputAttemptTick;
        event.kind = EventKind::scriptableOverrideTransportStaged;
        event.sourceGeneration = sourceGeneration;
        // The tail rode out on this same body, so it stages with the head or not at all.
        for (std::size_t index = 0; index < instance->pendingScriptableTailCount; ++index) {
            const PendingScriptableOverride& queued = instance->pendingScriptableTail[index];
            ScriptableGuard* const queuedGuard = find_guard(*instance, queued.target);
            if (!staged_counter_matches(queuedGuard, queued)
                || !retain_scriptable_auth(*instance, queued, sourceGeneration)) {
                ++g_refusedControls;
                continue;
            }
            advance_staged_guard(queuedGuard, queued);
            event.scriptableRevision = queued.revision;
            append_event(event);
        }
        instance->pendingScriptableTail.fill({});
        instance->pendingScriptableTailCount = 0;
        instance->view.scriptableTransportRevision = instance->view.scriptableRevision;
        instance->view.lastOutputSourceGeneration = sourceGeneration;
        ++instance->view.outputAttempts;
        instance->view.outputStatus = OutputStatus::transportStaged;
        instance->view.outputPending = false;
        instance->view.outputKind = OutputKind::none;
        instance->pendingScriptable = {};
        event.scriptableRevision = pending.revision;
        append_event(event);
        instance->view.lastEventSequence = g_sequence;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Copies the pending overrides that have no output yet. @return How many were written. */
std::size_t pending_scriptable_tail(const state::activity::SessionBinding& binding,
                                    std::span<PendingScriptableOverride> output) noexcept {
    AcquireSRWLockShared(&g_lock);
    const Instance* const instance = find_instance(binding);
    std::size_t written = 0;
    if (instance != nullptr && instance->view.active && instance->view.outputPending
        && instance->view.outputKind == OutputKind::scriptableOverride) {
        const std::size_t count = (std::min)(instance->pendingScriptableTailCount, output.size());
        for (; written < count; ++written) {
            output[written] = instance->pendingScriptableTail[written];
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return written;
}

/** @return True when any instance still owes a Host output. */
bool any_output_pending() noexcept {
    AcquireSRWLockShared(&g_lock);
    bool pending = false;
    for (const Instance& instance : g_instances) {
        pending =
            pending
            || (instance.occupied && instance.view.active
                && (instance.view.outputPending || has_queued_control(instance.view.binding)));
    }
    ReleaseSRWLockShared(&g_lock);
    return pending;
}

/** Copies the retained Auth estate for one exact ActivityClient generation. */
bool scriptable_auth_estate(const state::activity::SessionBinding& binding,
                            std::uint64_t activityClientGeneration,
                            std::vector<PendingScriptableOverride>& output) noexcept {
    output.clear();
    if (activityClientGeneration == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    const Instance* instance = nullptr;
    for (const Instance& candidate : g_instances) {
        if (candidate.occupied && same_binding(candidate.view.binding, binding)) {
            instance = &candidate;
            break;
        }
    }
    bool copied = true;
    if (instance != nullptr && instance->view.active) {
        try {
            output.reserve(instance->scriptableAuthEstate.size());
            for (const PendingScriptableOverride& retained : instance->scriptableAuthEstate) {
                // The ActivityClient generation is a transport revision that advances on ordinary
                // region advertisements, and the SessionBinding already owns estate lifetime.
                // Filtering retained mission state by it erases every non-squad Auth lane.
                output.push_back(retained);
            }
        } catch (const std::bad_alloc&) {
            output.clear();
            copied = false;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return copied;
}

} // namespace sunrise::server::activity::host
