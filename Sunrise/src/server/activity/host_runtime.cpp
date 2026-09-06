#include "../../middleware/bap/activity_message/sense_observation_packet.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <new>
#include <utility>

#include "../../core/logging/log.h"
#include "../../middleware/bap/activity_message/activity_entity_slot_request_parser.h"
#include "../../middleware/bap/activity_message/client_authoritative_data.h"
#include "../../state/activity/mission/runtime.h"
#include "../../state/activity/runtime.h"
#include "../../state/activity_sdk/format.h"
#include "../../state/activity_sdk/runtime.h"
#include "host_runtime_internal.h"

namespace sunrise::server::activity::host {
namespace detail {

struct MissionInputRecord final {
    MissionInputEvent view{};
    middleware::bap::activity_message::sense_update::DecodedPacket sense{};
    ClientMessageSnapshot clientMessage{};
    bool hasSense{};
    bool hasClientMessage{};
};

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<Instance, kInstanceCapacity> g_instances{};
std::vector<PendingInput> g_pending{};
std::array<Event, kEventCapacity> g_events{};
std::vector<MissionInputRecord> g_missionInputs{};
std::array<IncidentRecord, kIncidentHistoryCapacity> g_incidents{};
std::array<ClientMessageRecord, kClientMessageHistoryCapacity> g_clientMessages{};
std::array<ClientMessageDetail, kClientMessageDetailCapacity> g_clientMessageDetails{};
std::size_t g_pendingRead{};
std::size_t g_queuedIngress{};
std::size_t g_queuedControls{};
std::size_t g_eventStart{};
std::size_t g_eventCount{};
std::size_t g_clientMessageStart{};
std::size_t g_clientMessageCount{};
std::size_t g_clientMessageDetailStart{};
std::size_t g_clientMessageDetailCount{};
std::uint64_t g_sequence{};
std::uint64_t g_scriptableReservationGeneration{1};
std::uint64_t g_scriptableReservationSequence{};
std::uint64_t g_missionInputSequence{};
std::uint64_t g_eventGeneration{1};
std::uint64_t g_clientMessageSequence{};
std::uint64_t g_touch{};
std::uint64_t g_droppedIngress{};
std::uint64_t g_droppedIncidents{};
std::uint64_t g_refusedControls{};
std::uint64_t g_refusedIncidents{};
std::uint64_t g_overwrittenEvents{};
std::uint64_t g_overwrittenIncidents{};
std::uint64_t g_overwrittenClientMessages{};

/** @return True when the value stays inside the client's jump table. */
[[nodiscard]] bool lifetime_allowed(std::uint8_t value) noexcept {
    return value <= kMaximumLifetimeState;
}

/** @return True when the shared codec accepts every bounded outer incident field. */
[[nodiscard]] bool
incident_allowed(const middleware::bap::activity_message::incident::Incident& incident) noexcept {
    return middleware::bap::activity_message::incident::outer_valid(incident);
}

/** Advances a diagnostic counter without making zero look like no event. */
[[nodiscard]] std::uint64_t next_nonzero(std::uint64_t value) noexcept {
    return value == (std::numeric_limits<std::uint64_t>::max)() ? 1 : value + 1;
}

/** Finds one exact instance while the runtime lock is held. */
[[nodiscard]] Instance* find_instance(const state::activity::SessionBinding& binding) noexcept {
    for (Instance& instance : g_instances) {
        if (instance.occupied && same_binding(instance.view.binding, binding)) {
            return &instance;
        }
    }
    return nullptr;
}

/** @return True when this exact binding already has an operator request waiting to reduce. */
[[nodiscard]] bool has_queued_control(const state::activity::SessionBinding& binding) noexcept {
    for (std::size_t index = g_pendingRead; index < g_pending.size(); ++index) {
        const PendingInput& pending = g_pending[index];
        if (pending.kind == PendingKind::authControl
            && same_binding(pending.control.binding, binding)) {
            return true;
        }
        if (pending.kind == PendingKind::incidentControl
            && same_binding(pending.incidentControl.binding, binding)) {
            return true;
        }
        if (pending.kind == PendingKind::scriptableControl
            && same_binding(pending.scriptableControl.binding, binding)) {
            return true;
        }
    }
    return false;
}

/** A refused input is the one loss the host cannot see later, so it says so when it happens. */
void report_ingress_drop(PendingKind kind, const char* reason) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=activity stage=ingress result=drop reason=%s kind=%u queued=%zu",
                      reason,
                      static_cast<unsigned>(kind),
                      g_pending.size());
    if (written <= 0) {
        return;
    }
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(core::log::Channel::server, core::log::Level::debug, {line.data(), length});
}

/** Appends one owned reducer row while the runtime lock is held. @return False when full. */
bool append_pending(PendingInput&& pending) noexcept {
    if (g_pending.size() == g_pending.max_size()) {
        report_ingress_drop(pending.kind, "queue_full");
        return false;
    }
    try {
        g_pending.push_back(std::move(pending));
    } catch (const std::bad_alloc&) {
        report_ingress_drop(pending.kind, "no_memory");
        return false;
    }
    return true;
}

/** Allocates one diagnostic instance without evicting a binding active in this service slice. */
[[nodiscard]] Instance* ensure_instance(const state::activity::SessionBinding& binding) noexcept {
    if (Instance* const current = find_instance(binding); current != nullptr) {
        return current;
    }
    Instance* selected = nullptr;
    for (Instance& instance : g_instances) {
        if (!instance.occupied) {
            selected = &instance;
            break;
        }
        if (!instance.view.active && !instance.view.outputPending
            && !instance.view.scriptableReservationPending && instance.view.incidentsPending == 0
            && (selected == nullptr || instance.lastTouched < selected->lastTouched)) {
            selected = &instance;
        }
    }
    if (selected == nullptr) {
        return nullptr;
    }
    clear_instance(*selected);
    selected->occupied = true;
    selected->view.binding = binding;
    selected->view.lifetimeState = kDefaultLifetimeState;
    return selected;
}

/** Finds one retained outbound incident revision while the runtime lock is held. */
[[nodiscard]] IncidentRecord* find_incident(const state::activity::SessionBinding& binding,
                                            std::uint64_t revision) noexcept {
    for (IncidentRecord& record : g_incidents) {
        if (record.sequence != 0 && record.outbound && record.revision == revision
            && same_binding(record.binding, binding)) {
            return &record;
        }
    }
    return nullptr;
}

/** Reserves a full incident record without discarding an unstaged operator event. */
[[nodiscard]] IncidentRecord* reserve_incident_record() noexcept {
    IncidentRecord* selected = nullptr;
    for (IncidentRecord& record : g_incidents) {
        if (record.sequence == 0) {
            return &record;
        }
        const bool evictable = !record.outbound || record.transportStages != 0
                               || record.status == IncidentStatus::canceled;
        if (evictable && (selected == nullptr || record.sequence < selected->sequence)) {
            selected = &record;
        }
    }
    if (selected != nullptr) {
        ++g_overwrittenIncidents;
    }
    return selected;
}

/** Copies one incident summary into the chronological event history. */
void fill_incident_event(Event& event,
                         const middleware::bap::activity_message::incident::Incident& incident,
                         std::uint64_t revision) noexcept {
    event.incidentRevision = revision;
    event.incidentTarget = incident.primaryTarget;
    event.incidentExtraTargets = incident.extraTargetCount;
    event.incidentSelectorBytes = incident.selectorLength;
    event.incidentPayloadBytes = incident.payloadLength;
}

/** Moves one instance to the newest eviction position. */
void touch(Instance& instance) noexcept {
    g_touch = next_nonzero(g_touch);
    instance.lastTouched = g_touch;
}

/** @return True when this event kind enters the ordered mission-input feed. */
[[nodiscard]] bool mission_input_kind(EventKind kind) noexcept {
    return kind == EventKind::senseUpdate || kind == EventKind::incidentReceived
           || kind == EventKind::clientStateChanged || kind == EventKind::entitySlotsRequested
           || kind == EventKind::clientMessageReceived;
}

/**
 * @return True when the feed reserved room for one more accepted row. The feed holds every
 * accepted row until its program commits it, so it has no row count of its own. Only the
 * allocator can refuse.
 */
[[nodiscard]] bool reserve_mission_input_slot() noexcept {
    if (g_missionInputs.size() < g_missionInputs.capacity()) {
        return true;
    }
    // Grow a page at a time so the append that spends a durable sequence cannot reallocate.
    try {
        g_missionInputs.reserve(g_missionInputs.capacity() + kMissionInputReadPageSize);
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

/** Reports one client input the mission-input feed could not store. */
void report_mission_input_refusal(const Event& event) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=mission_input result=allocation_refused session=0x%llX binding_rev=%llu "
                      "kind=%u retained=%zu",
                      static_cast<unsigned long long>(event.binding.sessionId),
                      static_cast<unsigned long long>(event.binding.createdRevision),
                      static_cast<unsigned>(event.kind),
                      g_missionInputs.size());
    if (written > 0) {
        core::log::write(
            core::log::Channel::server,
            core::log::Level::debug,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1)});
    }
}

/** Assigns one exact binding's ordered client mission-input sequence. */
void stamp_mission_sequence(Event& event) noexcept {
    if (!mission_input_kind(event.kind)) {
        return;
    }
    Instance* const instance = find_instance(event.binding);
    if (instance == nullptr) {
        return;
    }
    // The durable sequence is what makes a row owed, so refuse before spending it.
    if (!reserve_mission_input_slot()) {
        report_mission_input_refusal(event);
        return;
    }
    std::uint64_t sequence = 0;
    if (state::activity::mission::issue_input_sequence(event.binding, sequence)) {
        instance->missionSequence = sequence;
        event.missionSequence = sequence;
    }
}

/** Appends one event in oldest-to-newest ring order. */
void append_event(Event& event) noexcept {
    stamp_mission_sequence(event);
    g_sequence = next_nonzero(g_sequence);
    event.sequence = g_sequence;
    std::size_t index = (g_eventStart + g_eventCount) % g_events.size();
    if (g_eventCount == g_events.size()) {
        index = g_eventStart;
        g_eventStart = (g_eventStart + 1) % g_events.size();
        ++g_overwrittenEvents;
    } else {
        ++g_eventCount;
    }
    g_events[index] = event;
}

/** Appends framing metadata without entering or draining the reducer queue. */
[[nodiscard]] std::uint64_t append_client_message(ClientMessageRecord record) noexcept {
    g_clientMessageSequence = next_nonzero(g_clientMessageSequence);
    record.sequence = g_clientMessageSequence;
    std::size_t index = (g_clientMessageStart + g_clientMessageCount) % g_clientMessages.size();
    if (g_clientMessageCount == g_clientMessages.size()) {
        index = g_clientMessageStart;
        g_clientMessageStart = (g_clientMessageStart + 1) % g_clientMessages.size();
        ++g_overwrittenClientMessages;
    } else {
        ++g_clientMessageCount;
    }
    g_clientMessages[index] = record;
    return record.sequence;
}

/** Appends one bounded decode in oldest-to-newest ring order. */
void append_client_message_detail(
    std::uint64_t sequence,
    std::uint32_t messageType,
    const middleware::bap::activity_message::sense_update::DecodedPacket* sense) noexcept {
    std::size_t index =
        (g_clientMessageDetailStart + g_clientMessageDetailCount) % g_clientMessageDetails.size();
    if (g_clientMessageDetailCount == g_clientMessageDetails.size()) {
        index = g_clientMessageDetailStart;
        g_clientMessageDetailStart =
            (g_clientMessageDetailStart + 1) % g_clientMessageDetails.size();
    } else {
        ++g_clientMessageDetailCount;
    }
    ClientMessageDetail& detail = g_clientMessageDetails[index];
    detail = {};
    detail.sequence = sequence;
    detail.messageType = messageType;
    if (sense != nullptr) {
        detail.sense = *sense;
        detail.hasSenseDecode = true;
    }
}

/** Cancels one committed output while the runtime lock is held. */
void cancel_output(Instance& instance, std::uint64_t now) noexcept {
    if (!instance.view.outputPending) {
        return;
    }
    Event event{};
    event.binding = instance.view.binding;
    event.tick = now;
    if (instance.view.outputKind == OutputKind::incident) {
        IncidentRecord* const record =
            find_incident(instance.view.binding, instance.view.incidentRevision);
        if (record != nullptr) {
            record->status = IncidentStatus::canceled;
            fill_incident_event(event, record->incident, record->revision);
        }
        event.kind = EventKind::incidentCanceled;
        instance.view.incidentsPending = 0;
    } else if (instance.view.outputKind == OutputKind::scriptableOverride) {
        event.kind = EventKind::scriptableOverrideCanceled;
        event.scriptableRevision = instance.view.scriptableRevision;
        instance.pendingScriptable = {};
    } else {
        event.stateRevision = instance.view.stateRevision;
        event.lifetimeState = instance.view.lifetimeState;
        event.kind = EventKind::authStateCanceled;
    }
    instance.view.outputPending = false;
    instance.view.outputKind = OutputKind::none;
    instance.view.outputStatus = OutputStatus::canceled;
    append_event(event);
    instance.view.lastEventSequence = g_sequence;
}

/** @return True when one decoded object has the exact retained observation key. */
[[nodiscard]] bool same_sense_key(
    const SenseObservationKey& key,
    const middleware::bap::activity_message::sense_update::DecodedObject& object) noexcept {
    return key.registryKey == object.registryKey && key.objectTag == object.objectTag
           && key.slotType == object.slotType && key.slotIndex == object.slotIndex
           && key.senseSchema == object.senseSchema && key.schemaRow == object.schemaRow;
}

/** @return True when the packet carries this key at or after the selected object. */
[[nodiscard]] bool
packet_has_sense_key(const middleware::bap::activity_message::sense_update::DecodedPacket& packet,
                     const SenseObservationKey& key,
                     std::size_t first) noexcept {
    for (std::size_t index = first; index < packet.objectCount; ++index) {
        if (packet.objects[index].status == middleware::bap::activity_message::sense_update::ObjectStatus::decoded
            && packet.objects[index].hasGeneration && same_sense_key(key, packet.objects[index])) {
            return true;
        }
    }
    return false;
}

/** @return True when the envelope closed and each decoded object's storage is bounded. */
[[nodiscard]] bool valid_sense_observation_input(const SenseInput& input) noexcept {
    namespace sense = middleware::bap::activity_message::sense_update;
    const auto& packet = input.decoded;
    return input.sourceGeneration != 0 && input.clientMessageSequence != 0
        && input.verdict == state::activity::receipts::Verdict::framed
        && input.decodeStatus == packet.status && sense::observation_packet(packet)
        && input.groupsSeen == packet.groupsSeen && input.groupsDecoded == packet.groupsDecoded
        && input.groupsSkipped == packet.groupsSkipped && input.objectsSeen == packet.objectsSeen
        && input.objectsDecoded == packet.objectsDecoded;
}

/** Retains one accepted client input independently from panel and output events. */
void append_mission_input(
    const Event& event,
    const middleware::bap::activity_message::sense_update::DecodedPacket* sense,
    const ClientMessageSnapshot* clientMessage = nullptr) noexcept {
    if (event.missionSequence == 0) {
        return;
    }
    g_missionInputSequence = next_nonzero(g_missionInputSequence);
    MissionInputRecord record{};
    record.view.event = event;
    record.view.sequence = g_missionInputSequence;
    if (record.view.event.sequence == 0) {
        record.view.event.sequence = g_missionInputSequence;
    }
    if (sense != nullptr) {
        record.sense = *sense;
        record.hasSense = true;
    }
    if (clientMessage != nullptr) {
        record.clientMessage = *clientMessage;
        record.hasClientMessage = true;
    }
    // The slot was reserved when the durable sequence was issued, so this never reallocates.
    g_missionInputs.push_back(std::move(record));
}

/** Mixes one fixed-width value into the local scene change guard. */
void mix_scene_fingerprint(std::uint64_t& fingerprint, std::uint64_t value) noexcept {
    constexpr std::uint64_t kPrime = 1'099'511'628'211ULL;
    for (std::uint8_t shift = 0; shift < 64; shift += 8) {
        fingerprint ^= (value >> shift) & 0xFFU;
        fingerprint *= kPrime;
    }
}

/** @return A run-local change guard over one decoded object and every retained value. */
[[nodiscard]] std::uint64_t
scene_fingerprint(const middleware::bap::activity_message::sense_update::DecodedObject& object,
                  std::span<const middleware::bap::activity_message::sense_update::DecodedValue>
                      values) noexcept {
    constexpr std::uint64_t kOffset = 14'695'981'039'346'656'037ULL;
    std::uint64_t fingerprint = kOffset;
    mix_scene_fingerprint(fingerprint, object.objectRow);
    mix_scene_fingerprint(fingerprint, object.slotRow);
    mix_scene_fingerprint(fingerprint, object.schemaRow);
    mix_scene_fingerprint(fingerprint, object.generationPlusOne);
    mix_scene_fingerprint(fingerprint, object.deltaBits);
    mix_scene_fingerprint(fingerprint, object.hasGeneration ? 1U : 0U);
    mix_scene_fingerprint(fingerprint, values.size());
    for (const auto& value : values) {
        mix_scene_fingerprint(fingerprint, value.unsignedValue);
        mix_scene_fingerprint(fingerprint, static_cast<std::uint64_t>(value.signedValue));
        mix_scene_fingerprint(fingerprint, std::bit_cast<std::uint32_t>(value.realValue));
        mix_scene_fingerprint(fingerprint, value.schemaRow);
        mix_scene_fingerprint(fingerprint, value.fieldRow);
        mix_scene_fingerprint(fingerprint, value.occurrence);
        mix_scene_fingerprint(fingerprint, value.bitOffset);
        mix_scene_fingerprint(fingerprint, value.fieldOrdinal);
        mix_scene_fingerprint(fingerprint, value.width);
        mix_scene_fingerprint(fingerprint, static_cast<std::uint8_t>(value.kind));
        mix_scene_fingerprint(fingerprint, value.present ? 1U : 0U);
    }
    return fingerprint;
}

/** Writes one bounded type-43 diagnostic event. */
void report_scene_sense(const char* format, ...) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(line.data(), line.size(), format, arguments);
    va_end(arguments);
    if (written <= 0) {
        return;
    }
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(core::log::Channel::server, core::log::Level::debug, {line.data(), length});
}

/** @return The trace row for one exact ClientRef, or null when it has not been seen. */
[[nodiscard]] SceneSenseTraceRecord* find_scene_trace_record(
    SceneSenseTrace& trace,
    const middleware::bap::activity_message::sense_update::DecodedObject& object) noexcept {
    for (SceneSenseTraceRecord& record : trace.records) {
        if (record.occupied && same_sense_key(record.key, object)) {
            return &record;
        }
    }
    return nullptr;
}

/** @return One unused trace row, or null when the bounded table is full. */
[[nodiscard]] SceneSenseTraceRecord* reserve_scene_trace_record(SceneSenseTrace& trace) noexcept {
    for (SceneSenseTraceRecord& record : trace.records) {
        if (!record.occupied) {
            return &record;
        }
    }
    return nullptr;
}

/** Reports one typed scalar with its exact reflected rows and wire position. */
void report_scene_value(
    const Instance& instance,
    const SenseInput& input,
    const middleware::bap::activity_message::sense_update::DecodedObject& object,
    const middleware::bap::activity_message::sense_update::DecodedValue& value,
    std::size_t valueIndex) noexcept {
    using ValueKind = middleware::bap::activity_message::sense_update::ValueKind;
    constexpr const char* kPrefix =
        "ev=scene_sense kind=value activity=%d session=0x%llX binding_rev=%llu "
        "source_gen=%llu msg_seq=%llu key=0x%08X tag=0x%08X type=%u index=%u "
        "sense_schema=0x%08X gen_plus_one=%u value_index=%zu schema_row=%u "
        "field_row=%u ordinal=%u occurrence=%u bit=%u width=%u present=%u";
    std::array<char, core::log::kLineCapacity> prefix{};
    const int written =
        std::snprintf(prefix.data(),
                      prefix.size(),
                      kPrefix,
                      static_cast<int>(instance.view.binding.destination.activityIndex),
                      static_cast<unsigned long long>(instance.view.binding.sessionId),
                      static_cast<unsigned long long>(instance.view.binding.createdRevision),
                      static_cast<unsigned long long>(input.sourceGeneration),
                      static_cast<unsigned long long>(input.clientMessageSequence),
                      object.registryKey,
                      object.objectTag,
                      static_cast<unsigned>(object.slotType),
                      static_cast<unsigned>(object.slotIndex),
                      object.senseSchema,
                      object.generationPlusOne,
                      valueIndex,
                      value.schemaRow,
                      value.fieldRow,
                      static_cast<unsigned>(value.fieldOrdinal),
                      value.occurrence,
                      value.bitOffset,
                      static_cast<unsigned>(value.width),
                      value.present ? 1U : 0U);
    if (written <= 0 || static_cast<std::size_t>(written) >= prefix.size()) {
        return;
    }
    if (!value.present) {
        report_scene_sense("%s domain=%s value=absent",
                           prefix.data(),
                           value.kind == ValueKind::unsignedInteger ? "uint"
                           : value.kind == ValueKind::signedInteger ? "int"
                           : value.kind == ValueKind::boolean       ? "bool"
                                                                    : "real32");
        return;
    }
    switch (value.kind) {
    case ValueKind::unsignedInteger:
        report_scene_sense("%s domain=uint value=0x%llX",
                           prefix.data(),
                           static_cast<unsigned long long>(value.unsignedValue));
        break;
    case ValueKind::signedInteger:
        report_scene_sense("%s domain=int raw=0x%llX value=%lld",
                           prefix.data(),
                           static_cast<unsigned long long>(value.unsignedValue),
                           static_cast<long long>(value.signedValue));
        break;
    case ValueKind::boolean:
        report_scene_sense("%s domain=bool raw=0x%llX value=%u",
                           prefix.data(),
                           static_cast<unsigned long long>(value.unsignedValue),
                           value.unsignedValue != 0 ? 1U : 0U);
        break;
    case ValueKind::real32:
        report_scene_sense("%s domain=real32 raw=0x%llX value_bits=0x%08X value=%.9g",
                           prefix.data(),
                           static_cast<unsigned long long>(value.unsignedValue),
                           std::bit_cast<std::uint32_t>(value.realValue),
                           static_cast<double>(value.realValue));
        break;
    }
}

/** Reports complete changed type-43 objects without changing retained activity state. */
void trace_scene_sense(Instance& instance, const SenseInput& input) noexcept {
    namespace sense = middleware::bap::activity_message::sense_update;
    if (!core::log::accepts(core::log::Channel::server, core::log::Level::debug)) {
        return;
    }
    SceneSenseTrace& trace = instance.sceneSenseTrace;
    if (trace.sourceGeneration != input.sourceGeneration) {
        trace = {};
        trace.sourceGeneration = input.sourceGeneration;
    }
    const sense::DecodedPacket& packet = input.decoded;
    bool hasScene = false;
    const std::size_t retainedObjectCount = (std::min)(packet.objectCount, packet.objects.size());
    for (std::size_t index = 0; index < retainedObjectCount; ++index) {
        if (packet.objects[index].slotType
            == static_cast<std::uint8_t>(state::activity_sdk::format::kAuthoredSceneSlotType)) {
            hasScene = true;
            break;
        }
    }
    if (!valid_sense_observation_input(input)) {
        if (!trace.incompleteReported && (hasScene || packet.objectsTruncated)) {
            trace.incompleteReported = true;
            report_scene_sense(
                "ev=scene_sense kind=packet result=skip reason=incomplete activity=%d "
                "session=0x%llX binding_rev=%llu source_gen=%llu msg_seq=%llu "
                "status=%s objects_seen=%u objects_kept=%zu values_kept=%zu "
                "objects_truncated=%u values_truncated=%u",
                static_cast<int>(instance.view.binding.destination.activityIndex),
                static_cast<unsigned long long>(instance.view.binding.sessionId),
                static_cast<unsigned long long>(instance.view.binding.createdRevision),
                static_cast<unsigned long long>(input.sourceGeneration),
                static_cast<unsigned long long>(input.clientMessageSequence),
                sense::decode_status_name(packet.status),
                packet.objectsSeen,
                packet.objectCount,
                packet.valueCount,
                packet.objectsTruncated ? 1U : 0U,
                packet.valuesTruncated ? 1U : 0U);
        }
        return;
    }
    for (std::size_t index = 0; index < packet.objectCount; ++index) {
        const sense::DecodedObject& object = packet.objects[index];
        if (object.status != sense::ObjectStatus::decoded || !object.hasGeneration || object.slotType
            != static_cast<std::uint8_t>(state::activity_sdk::format::kAuthoredSceneSlotType)
            || packet_has_sense_key(packet,
                                    {object.registryKey,
                                     object.objectTag,
                                     object.senseSchema,
                                     object.schemaRow,
                                     object.slotIndex,
                                     object.slotType},
                                    index + 1)) {
            continue;
        }
        const std::span values(packet.values.data() + object.firstValue, object.valueCount);
        const std::uint64_t fingerprint = scene_fingerprint(object, values);
        SceneSenseTraceRecord* record = find_scene_trace_record(trace, object);
        const bool known = record != nullptr;
        if (known && record->fingerprint == fingerprint
            && record->generationPlusOne == object.generationPlusOne
            && record->valueCount == object.valueCount
            && record->hasGeneration == object.hasGeneration) {
            continue;
        }
        if (record == nullptr) {
            record = reserve_scene_trace_record(trace);
        }
        if (record == nullptr) {
            if (!trace.capacityReported) {
                trace.capacityReported = true;
                report_scene_sense(
                    "ev=scene_sense kind=packet result=skip reason=trace_capacity activity=%d "
                    "session=0x%llX binding_rev=%llu source_gen=%llu capacity=%zu",
                    static_cast<int>(instance.view.binding.destination.activityIndex),
                    static_cast<unsigned long long>(instance.view.binding.sessionId),
                    static_cast<unsigned long long>(instance.view.binding.createdRevision),
                    static_cast<unsigned long long>(input.sourceGeneration),
                    trace.records.size());
            }
            continue;
        }
        record->key = {object.registryKey,
                       object.objectTag,
                       object.senseSchema,
                       object.schemaRow,
                       object.slotIndex,
                       object.slotType};
        record->fingerprint = fingerprint;
        record->generationPlusOne = object.generationPlusOne;
        record->valueCount = object.valueCount;
        record->hasGeneration = object.hasGeneration;
        record->occupied = true;
        report_scene_sense("ev=scene_sense kind=object change=%s activity=%d session=0x%llX "
                           "binding_rev=%llu source_gen=%llu msg_seq=%llu key=0x%08X tag=0x%08X "
                           "object_row=%u type=%u index=%u slot_row=%u sense_schema=0x%08X "
                           "schema_row=%u gen_plus_one=%u has_gen=%u delta_bits=%u values=%u",
                           known ? "update" : "new",
                           static_cast<int>(instance.view.binding.destination.activityIndex),
                           static_cast<unsigned long long>(instance.view.binding.sessionId),
                           static_cast<unsigned long long>(instance.view.binding.createdRevision),
                           static_cast<unsigned long long>(input.sourceGeneration),
                           static_cast<unsigned long long>(input.clientMessageSequence),
                           object.registryKey,
                           object.objectTag,
                           object.objectRow,
                           static_cast<unsigned>(object.slotType),
                           static_cast<unsigned>(object.slotIndex),
                           object.slotRow,
                           object.senseSchema,
                           object.schemaRow,
                           object.generationPlusOne,
                           object.hasGeneration ? 1U : 0U,
                           object.deltaBits,
                           object.valueCount);
        for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
            report_scene_value(instance, input, object, values[valueIndex], valueIndex);
        }
    }
}

/** Appends one observation and its complete owned value range. */
[[nodiscard]] bool append_sense_observation(
    SenseObservationSnapshot& output,
    SenseObservation observation,
    std::span<const middleware::bap::activity_message::sense_update::DecodedValue>
        values) noexcept {
    if (output.observationCount == output.observations.size()
        || values.size() > output.values.size() - output.valueCount) {
        return false;
    }
    observation.firstValue = static_cast<std::uint32_t>(output.valueCount);
    observation.valueCount = static_cast<std::uint32_t>(values.size());
    output.observations[output.observationCount++] = observation;
    std::copy(values.begin(), values.end(), output.values.begin() + output.valueCount);
    output.valueCount += values.size();
    return true;
}

/** Replaces only fully decoded keys in an accepted packet; omitted or unsupported keys stay retained. */
[[nodiscard]] bool
retain_sense_observations(Instance& instance, const SenseInput& input, std::uint64_t now) noexcept {
    namespace sense = middleware::bap::activity_message::sense_update;
    if (!valid_sense_observation_input(input)) {
        return false;
    }
    const sense::DecodedPacket& packet = input.decoded;
    SenseObservationSnapshot next{};
    next.revision = next_nonzero(instance.senseObservations.revision);
    next.sourceGeneration = input.sourceGeneration;
    for (std::size_t index = 0; index < packet.objectCount; ++index) {
        const sense::DecodedObject& object = packet.objects[index];
        if (object.status != sense::ObjectStatus::decoded || !object.hasGeneration) continue;
        const SenseObservationKey key{object.registryKey,
                                      object.objectTag,
                                      object.senseSchema,
                                      object.schemaRow,
                                      object.slotIndex,
                                      object.slotType};
        if (packet_has_sense_key(packet, key, index + 1)) {
            continue;
        }
        SenseObservation observation{};
        observation.binding = input.binding;
        observation.key = key;
        observation.sequence = next.revision;
        observation.tick = now;
        observation.sourceGeneration = input.sourceGeneration;
        observation.clientMessageSequence = input.clientMessageSequence;
        observation.generationPlusOne = object.generationPlusOne;
        observation.hasGeneration = object.hasGeneration;
        const std::span values(packet.values.data() + object.firstValue, object.valueCount);
        if (!append_sense_observation(next, observation, values)) {
            return false;
        }
    }
    if (instance.senseObservations.sourceGeneration == input.sourceGeneration) {
        const SenseObservationSnapshot& current = instance.senseObservations;
        for (std::size_t index = 0; index < current.observationCount; ++index) {
            const SenseObservation& observation = current.observations[index];
            if (packet_has_sense_key(packet, observation.key, 0)
                || observation.firstValue > current.valueCount
                || observation.valueCount > current.valueCount - observation.firstValue) {
                continue;
            }
            const std::span values(current.values.data() + observation.firstValue,
                                   observation.valueCount);
            static_cast<void>(append_sense_observation(next, observation, values));
        }
    }
    instance.senseObservations = next;
    instance.view.senseObservationCount = static_cast<std::uint32_t>(next.observationCount);
    instance.view.senseObservationValueCount = static_cast<std::uint32_t>(next.valueCount);
    instance.view.senseObservationRevision = next.revision;
    instance.view.senseObservationSourceGeneration = next.sourceGeneration;
    return true;
}

/** Applies one copied msg-6 decode summary. */
void apply_sense(const SenseInput& input, std::uint64_t now) noexcept {
    Instance* const instance = find_instance(input.binding);
    if (instance == nullptr || !instance->view.active) {
        ++g_droppedIngress;
        return;
    }
    touch(*instance);
    ++instance->view.senseCount;
    trace_scene_sense(*instance, input);
    static_cast<void>(retain_sense_observations(*instance, input, now));
    Event event{};
    event.binding = input.binding;
    event.tick = now;
    event.kind = EventKind::senseUpdate;
    event.epochFirst = input.epochFirst;
    event.epochSecond = input.epochSecond;
    event.payloadBytes = input.payloadBytes;
    event.peerHeardMask = input.peerHeardMask;
    event.tailBits = input.tailBits;
    event.consumedBits = input.consumedBits;
    event.firstGroupBits = input.firstGroupBits;
    event.firstRegistryKey = input.firstRegistryKey;
    event.groupsSeen = input.groupsSeen;
    event.groupsDecoded = input.groupsDecoded;
    event.groupsSkipped = input.groupsSkipped;
    event.objectsSeen = input.objectsSeen;
    event.objectsDecoded = input.objectsDecoded;
    event.firstSlotIndex = input.firstSlotIndex;
    event.firstSlotType = input.firstSlotType;
    // The event names one slot only: the first decoded ClientRef of the packet.
    if (input.decoded.objectCount != 0) {
        event.slotObjectTag = input.decoded.objects.front().objectTag;
        event.slotSenseSchema = input.decoded.objects.front().senseSchema;
    }
    event.senseDecodeStatus = input.decodeStatus;
    event.senseSnapshotRetained = valid_sense_observation_input(input);
    event.hasFirstObject = input.hasFirstObject;
    event.stateRevision = instance->view.stateRevision;
    event.sourceGeneration = input.sourceGeneration;
    event.clientMessageSequence = input.clientMessageSequence;
    event.lifetimeState = instance->view.lifetimeState;
    event.verdict = input.verdict;
    append_event(event);
    append_mission_input(event, event.senseSnapshotRetained ? &input.decoded : nullptr);
    instance->view.lastEventSequence = g_sequence;
}

/** Retains one outer-valid client incident for inspection and parsed-field replay. */
void apply_incident(const IncidentInput& input, std::uint64_t now) noexcept {
    Instance* const instance = find_instance(input.binding);
    if (instance == nullptr || !instance->view.active) {
        ++g_droppedIngress;
        ++g_droppedIncidents;
        return;
    }
    touch(*instance);
    ++instance->view.incidentsReceived;
    Event event{};
    event.binding = input.binding;
    event.tick = now;
    event.kind = EventKind::incidentReceived;
    event.sourceGeneration = input.sourceGeneration;
    event.clientMessageSequence = input.clientMessageSequence;
    event.payloadBytes = input.payloadBytes;
    fill_incident_event(event, input.incident, 0);
    event.hasPlayerTrigger = input.hasPlayerTrigger;
    if (input.hasPlayerTrigger) {
        event.playerTriggerRegistryKey = input.playerTrigger.registryKey;
        event.playerTriggerSlotType = input.playerTrigger.slotType;
        event.playerTriggerSlotIndex = input.playerTrigger.slotIndex;
        event.playerTriggerResolvedObjectId = input.playerTrigger.resolvedObjectId;
    }
    event.hasCinematic = input.hasCinematic;
    if (input.hasCinematic) {
        event.cinematicRegistryKey = input.cinematic.registryKey;
        event.cinematicSlotType = input.cinematic.slotType;
        event.cinematicSlotIndex = input.cinematic.slotIndex;
        event.cinematicRuntimeObjectId = input.cinematic.runtimeObjectId;
        event.cinematicEventValue = input.cinematic.eventValue;
        event.cinematicSignal = input.cinematicSignal;
    }
    append_event(event);
    append_mission_input(event, nullptr);
    instance->view.lastEventSequence = g_sequence;

    IncidentRecord* const record = reserve_incident_record();
    if (record == nullptr) {
        ++g_droppedIncidents;
        return;
    }
    *record = {};
    record->binding = input.binding;
    record->incident = input.incident;
    record->sequence = g_sequence;
    record->tick = now;
    record->lastSourceGeneration = input.sourceGeneration;
    record->clientMessageSequence = input.clientMessageSequence;
    record->payloadBytes = input.payloadBytes;
    record->status = IncidentStatus::received;
}

/** Retains one safe committed client State change in Host and ordered mission histories. */
void apply_client_state_change(const ClientStateChangeInput& input, std::uint64_t now) noexcept {
    Instance* const instance = find_instance(input.binding);
    if (instance == nullptr || !instance->view.active) {
        ++g_droppedIngress;
        return;
    }
    touch(*instance);
    Event event{};
    event.binding = input.binding;
    event.tick = now;
    event.kind = EventKind::clientStateChanged;
    event.sourceGeneration = input.sourceGeneration;
    event.clientMessageSequence = input.clientMessageSequence;
    event.payloadBytes = input.payloadBytes;
    event.activityStateRevision = input.state.activityStateRevision;
    event.membershipRevision = input.state.membershipRevision;
    event.regionIndex = input.state.region.index;
    event.regionSliceSetHash = input.state.region.hash;
    event.currentRegionIndex = input.state.currentRegion.index;
    event.clientStateHasCurrentRegion = input.state.hasCurrentRegion;
    event.heldRegionIndex = input.state.heldRegion;
    event.teleportSliceSetIndex = input.state.teleportSliceSetIndex;
    event.teleportSliceSetHash = input.state.teleportSliceSetHash;
    event.spawnState = input.state.spawnState;
    event.teleportState = input.state.teleportState;
    event.clientStateHasRegion = input.state.hasRegion;
    event.clientStateHasSpawn = input.state.hasSpawn;
    event.clientStateHasTeleport = input.state.hasTeleport;
    append_event(event);
    append_mission_input(event, nullptr);
    instance->view.lastEventSequence = g_sequence;
}

/** Publishes one committed simulation-entity slot request without deriving readiness state. */
void apply_entity_slots_requested(const EntitySlotsRequestedInput& input,
                                  std::uint64_t now) noexcept {
    Instance* const instance = find_instance(input.binding);
    if (instance == nullptr || !instance->view.active) {
        ++g_droppedIngress;
        return;
    }
    touch(*instance);
    Event event{};
    event.binding = input.binding;
    event.tick = now;
    event.kind = EventKind::entitySlotsRequested;
    event.stateRevision = instance->view.stateRevision;
    event.sourceGeneration = input.sourceGeneration;
    event.clientMessageSequence = input.clientMessageSequence;
    event.clientMessageType = middleware::bap::activity_message::entity_slot_request::kMessageType;
    event.requestedEntitySlots = input.requestedCount;
    append_event(event);
    append_mission_input(event, nullptr);
    instance->view.lastEventSequence = g_sequence;
}

/** Publishes one safe generic client envelope into the ordered mission-input feed. */
void apply_client_message(const ClientMessageMissionInput& input, std::uint64_t now) noexcept {
    Instance* const instance = find_instance(input.binding);
    if (instance == nullptr || !instance->view.active) {
        ++g_droppedIngress;
        return;
    }
    touch(*instance);
    Event event{};
    event.binding = input.binding;
    event.tick = now;
    event.kind = EventKind::clientMessageReceived;
    event.stateRevision = instance->view.stateRevision;
    event.sourceGeneration = input.sourceGeneration;
    event.clientMessageSequence = input.clientMessageSequence;
    event.payloadBytes = input.payloadBytes;
    event.peerHeardMask = input.peerHeardMask;
    event.consumedBits = input.consumedBits;
    event.clientMessageType = input.messageType;
    event.clientMessageStatus = input.status;
    stamp_mission_sequence(event);
    ClientMessageSnapshot snapshot{};
    snapshot.messageType = input.messageType;
    snapshot.status = input.status;
    append_mission_input(event, nullptr, &snapshot);
}

/** Applies one operator transition only when its output slot is free. */
void apply_control(const ControlRequest& request, std::uint64_t now) noexcept {
    Event event{};
    event.binding = request.binding;
    event.tick = now;
    event.lifetimeState = request.lifetimeState;
    Instance* const instance = find_instance(request.binding);
    if (instance == nullptr || !instance->view.active) {
        event.kind = EventKind::operatorRefused;
        ++g_refusedControls;
    } else if (instance->view.outputPending) {
        event.kind = EventKind::operatorRefused;
        event.stateRevision = instance->view.stateRevision;
        ++g_refusedControls;
    } else if (instance->view.stateRevision == (std::numeric_limits<std::uint64_t>::max)()) {
        event.kind = EventKind::operatorRefused;
        event.stateRevision = instance->view.stateRevision;
        ++g_refusedControls;
    } else {
        touch(*instance);
        ++instance->view.stateRevision;
        instance->view.lifetimeState = request.lifetimeState;
        instance->view.lastOutputAttemptTick = 0;
        instance->view.lastOutputSourceGeneration = 0;
        instance->view.outputAttempts = 0;
        instance->view.outputStatus = OutputStatus::pending;
        instance->view.outputKind = OutputKind::authState;
        instance->view.outputPending = true;
        event.kind = EventKind::authStateCommitted;
        event.stateRevision = instance->view.stateRevision;
    }
    append_event(event);
    if (instance != nullptr) {
        instance->view.lastEventSequence = g_sequence;
    }
}

/** Commits one operator incident into the per-generation ordered output history. */
void apply_incident_control(const IncidentRequest& request, std::uint64_t now) noexcept {
    Event event{};
    event.binding = request.binding;
    event.tick = now;
    event.kind = EventKind::incidentRefused;
    fill_incident_event(event, request.incident, 0);
    Instance* const instance = find_instance(request.binding);
    IncidentRecord* record = nullptr;
    if (instance == nullptr || !instance->view.active || instance->view.outputPending
        || instance->view.incidentRevision == (std::numeric_limits<std::uint64_t>::max)()) {
        ++g_refusedControls;
        ++g_refusedIncidents;
    } else if ((record = reserve_incident_record()) == nullptr) {
        ++g_refusedControls;
        ++g_refusedIncidents;
    } else {
        touch(*instance);
        ++instance->view.incidentRevision;
        ++instance->view.incidentsQueued;
        ++instance->view.incidentsPending;
        instance->view.outputPending = true;
        instance->view.outputKind = OutputKind::incident;
        instance->view.outputStatus = OutputStatus::pending;
        instance->view.lastOutputAttemptTick = 0;
        instance->view.lastOutputSourceGeneration = 0;
        instance->view.outputAttempts = 0;
        *record = {};
        record->binding = request.binding;
        record->incident = request.incident;
        record->revision = instance->view.incidentRevision;
        record->tick = now;
        record->status = IncidentStatus::queued;
        record->outbound = true;
        event.kind = EventKind::incidentQueued;
        fill_incident_event(event, request.incident, record->revision);
    }
    append_event(event);
    if (record != nullptr && event.kind == EventKind::incidentQueued) {
        record->sequence = g_sequence;
    }
    if (instance != nullptr) {
        instance->view.lastEventSequence = g_sequence;
    }
}

} // namespace detail

using namespace detail;

/** Queues one owned msg-6 prefix for the Activity Host service. */
bool submit_sense(const SenseInput& input) noexcept {
    if (!state::activity::binding_matches(input.binding) || input.sourceGeneration == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    PendingInput pending{};
    pending.kind = PendingKind::sense;
    pending.sense = input;
    if (!append_pending(std::move(pending))) {
        ++g_droppedIngress;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    ++g_queuedIngress;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

/** Queues one owned, outer-valid client msg 19 for the Activity Host service. */
bool submit_incident(const IncidentInput& input) noexcept {
    if (!state::activity::binding_matches(input.binding) || !incident_allowed(input.incident)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    PendingInput pending{};
    pending.kind = PendingKind::incident;
    pending.incident = input;
    if (!append_pending(std::move(pending))) {
        ++g_droppedIngress;
        ++g_droppedIncidents;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    ++g_queuedIngress;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

/** Queues one post-commit client State after-image for the ordered Host service slice. */
bool submit_client_state_change(const ClientStateChangeInput& input) noexcept {
    // A report that moved no region, spawn or teleport field is the client's settle, sent once
    // spawn-in completes. The mission surface needs it to time the opening line, so only a
    // malformed report is refused, never a material-less one.
    if (!state::activity::binding_matches(input.binding) || input.sourceGeneration == 0
        || input.clientMessageSequence == 0 || !input.state.committed
        || (input.state.hasRegion && input.state.region.index < 0)
        || input.state.activityStateRevision == state::activity::kInvalidRevision) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    PendingInput pending{};
    pending.kind = PendingKind::clientStateChange;
    pending.clientStateChange = input;
    if (!append_pending(std::move(pending))) {
        ++g_droppedIngress;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    ++g_queuedIngress;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

/** Queues one committed msg-20 simulation-entity slot request. */
bool submit_entity_slots_requested(const EntitySlotsRequestedInput& input) noexcept {
    if (!state::activity::binding_matches(input.binding) || input.sourceGeneration == 0
        || input.clientMessageSequence == 0 || input.requestedCount <= 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    PendingInput pending{};
    pending.kind = PendingKind::entitySlotsRequested;
    pending.entitySlotsRequested = input;
    if (!append_pending(std::move(pending))) {
        ++g_droppedIngress;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    ++g_queuedIngress;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

/** Retains one owned client message without entering the reducer queue. */
std::uint64_t record_client_message(
    const ClientMessageInput& input,
    const middleware::bap::activity_message::sense_update::DecodedPacket* sense) noexcept {
    if (!state::activity::binding_matches(input.binding)) {
        return 0;
    }
    ClientMessageRecord record{};
    record.binding.sessionId = input.binding.sessionId;
    record.binding.createdRevision = input.binding.createdRevision;
    record.authoritative = input.authoritative;
    record.tick = GetTickCount64();
    record.sourceGeneration = input.sourceGeneration;
    record.payloadFingerprint = input.payloadFingerprint;
    record.messageType = input.messageType;
    record.payloadBytes = input.payloadBytes;
    record.peerHeardMask = input.peerHeardMask;
    record.consumedBits = input.consumedBits;
    record.status = input.status;
    record.hasPayloadFingerprint = input.hasPayloadFingerprint;
    record.hasAuthoritative = input.hasAuthoritative;
    AcquireSRWLockExclusive(&g_lock);
    const std::uint64_t sequence = append_client_message(record);
    if (sense != nullptr) {
        append_client_message_detail(sequence, input.messageType, sense);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return sequence;
}

/** Queues one owned client envelope without a richer typed mission reducer. */
bool submit_client_message(const ClientMessageInput& input,
                           std::uint64_t clientMessageSequence) noexcept {
    namespace activity_message = middleware::bap::activity_message;
    namespace communication = activity_message::wire_schema::communication;
    communication::ActivityCommunicationRoute route{};
    const bool executableRoute =
        state::activity_sdk::executable_communication_route(input.messageType, route);
    if (!state::activity::binding_matches(input.binding) || input.sourceGeneration == 0
        || clientMessageSequence == 0 || !executableRoute
        || route.ingressDelivery != communication::IngressDeliveryPolicy::protocolHostInput
        || (route.ingressClass != communication::IngressClass::nativeMetadataOnly
            && route.ingressClass != communication::IngressClass::nativeParsed)
        || input.messageType == activity_message::sense_update::kMessageType
        || input.messageType == activity_message::incident::kMessageType
        || input.messageType == activity_message::client_authoritative_data::kMessageType) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    PendingInput pending{};
    pending.kind = PendingKind::clientMessage;
    pending.clientMessage.binding = input.binding;
    pending.clientMessage.sourceGeneration = input.sourceGeneration;
    pending.clientMessage.clientMessageSequence = clientMessageSequence;
    pending.clientMessage.messageType = input.messageType;
    pending.clientMessage.payloadBytes = input.payloadBytes;
    pending.clientMessage.peerHeardMask = input.peerHeardMask;
    pending.clientMessage.consumedBits = input.consumedBits;
    pending.clientMessage.status = input.status;
    if (!append_pending(std::move(pending))) {
        ++g_droppedIngress;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    ++g_queuedIngress;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

/** Copies one retained scalar decode by its ingress sequence. */
bool client_message_detail(std::uint64_t sequence, ClientMessageDetail& output) noexcept {
    output = {};
    if (sequence == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    bool found = false;
    for (std::size_t offset = g_clientMessageDetailCount; offset != 0; --offset) {
        const std::size_t index =
            (g_clientMessageDetailStart + offset - 1) % g_clientMessageDetails.size();
        if (g_clientMessageDetails[index].sequence == sequence) {
            output = g_clientMessageDetails[index];
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

/** Queues one operator Auth-state transition for the exact activity generation. */
bool request_auth_state(const state::activity::SessionBinding& binding,
                        std::uint8_t lifetimeState) noexcept {
    if (!lifetime_allowed(lifetimeState) || !state::activity::binding_matches(binding)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    const Instance* const instance = find_instance(binding);
    if (has_queued_control(binding)
        || (instance != nullptr
            && (instance->view.outputPending || instance->view.scriptableReservationPending))) {
        ++g_refusedControls;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    PendingInput pending{};
    pending.kind = PendingKind::authControl;
    pending.control = {binding, lifetimeState};
    if (!append_pending(std::move(pending))) {
        ++g_refusedControls;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    ++g_queuedControls;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

/** Queues one outer-valid operator msg 19 for the exact activity generation. */
bool request_incident(
    const state::activity::SessionBinding& binding,
    const middleware::bap::activity_message::incident::Incident& incident) noexcept {
    if (!incident_allowed(incident) || !state::activity::binding_matches(binding)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    const Instance* const instance = find_instance(binding);
    if (has_queued_control(binding)
        || (instance != nullptr
            && (instance->view.outputPending || instance->view.scriptableReservationPending))) {
        ++g_refusedControls;
        ++g_refusedIncidents;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    PendingInput pending{};
    pending.kind = PendingKind::incidentControl;
    pending.incidentControl = {binding, incident};
    if (!append_pending(std::move(pending))) {
        ++g_refusedControls;
        ++g_refusedIncidents;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    ++g_queuedControls;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

/** Applies queued client and operator events on the server service slice. */
void service(std::uint64_t now) noexcept {
    std::array<state::activity::SessionBinding, state::activity::kSessionCapacity> bindings{};
    std::size_t bindingCount = 0;
    static_cast<void>(state::activity::snapshot_retained_bindings(bindings, bindingCount));

    AcquireSRWLockExclusive(&g_lock);
    for (Instance& instance : g_instances) {
        bool active = false;
        for (std::size_t index = 0; index < bindingCount; ++index) {
            if (same_binding(instance.view.binding, bindings[index])) {
                active = true;
                break;
            }
        }
        instance.view.active = instance.occupied && active;
        if (instance.occupied && !instance.view.active && instance.view.outputPending) {
            cancel_output(instance, now);
        }
        if (instance.occupied && !instance.view.active
            && instance.view.scriptableReservationPending) {
            instance.scriptableReservation = {};
            instance.view.scriptableReservedRevision = 0;
            instance.view.scriptableReservationPending = false;
        }
    }
    for (std::size_t index = 0; index < bindingCount; ++index) {
        Instance* const instance = ensure_instance(bindings[index]);
        if (instance != nullptr) {
            instance->view.active = true;
            touch(*instance);
        }
    }
    while (g_pendingRead < g_pending.size()) {
        PendingInput pending = std::move(g_pending[g_pendingRead]);
        ++g_pendingRead;
        if (pending.kind == PendingKind::discardedControl) {
            continue;
        }
        if (pending.kind == PendingKind::authControl) {
            --g_queuedControls;
            apply_control(pending.control, now);
        } else if (pending.kind == PendingKind::incidentControl) {
            --g_queuedControls;
            apply_incident_control(pending.incidentControl, now);
        } else if (pending.kind == PendingKind::scriptableControl) {
            --g_queuedControls;
            apply_scriptable_control(pending.scriptableControl, now);
        } else if (pending.kind == PendingKind::incident) {
            --g_queuedIngress;
            apply_incident(pending.incident, now);
        } else if (pending.kind == PendingKind::clientStateChange) {
            --g_queuedIngress;
            apply_client_state_change(pending.clientStateChange, now);
        } else if (pending.kind == PendingKind::entitySlotsRequested) {
            --g_queuedIngress;
            apply_entity_slots_requested(pending.entitySlotsRequested, now);
        } else if (pending.kind == PendingKind::clientMessage) {
            --g_queuedIngress;
            apply_client_message(pending.clientMessage, now);
        } else {
            --g_queuedIngress;
            apply_sense(pending.sense, now);
        }
    }
    g_pending.clear();
    g_pendingRead = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

/** Copies the latest complete diagnostic view. */
void snapshot(DiagnosticsSnapshot& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    for (const Instance& instance : g_instances) {
        if (instance.occupied && output.instanceCount < output.instances.size()) {
            output.instances[output.instanceCount] = instance.view;
            ++output.instanceCount;
        }
    }
    for (std::size_t index = 0; index < g_eventCount; ++index) {
        output.events[index] = g_events[(g_eventStart + index) % g_events.size()];
    }
    output.eventCount = g_eventCount;
    for (const IncidentRecord& record : g_incidents) {
        if (record.sequence != 0 && output.incidentCount < output.incidents.size()) {
            output.incidents[output.incidentCount++] = record;
        }
    }
    std::sort(output.incidents.begin(),
              output.incidents.begin() + static_cast<std::ptrdiff_t>(output.incidentCount),
              [](const IncidentRecord& left, const IncidentRecord& right) noexcept {
                  return left.sequence < right.sequence;
              });
    for (std::size_t index = 0; index < g_clientMessageCount; ++index) {
        output.clientMessages[index] =
            g_clientMessages[(g_clientMessageStart + index) % g_clientMessages.size()];
    }
    output.clientMessageCount = g_clientMessageCount;
    output.droppedIngress = g_droppedIngress;
    output.droppedIncidents = g_droppedIncidents;
    output.queuedControls = g_queuedControls;
    output.refusedControls = g_refusedControls;
    output.refusedIncidents = g_refusedIncidents;
    output.overwrittenEvents = g_overwrittenEvents;
    output.overwrittenIncidents = g_overwrittenIncidents;
    output.overwrittenClientMessages = g_overwrittenClientMessages;
    ReleaseSRWLockShared(&g_lock);
}

/** Copies one exact activity generation without exposing the Host lock. */
bool instance_snapshot(const state::activity::SessionBinding& binding,
                       InstanceSnapshot& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    const Instance* const instance = find_instance(binding);
    const bool found = instance != nullptr;
    if (found) {
        output = instance->view;
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

/** Reads the current feed position without replaying retained history. */
EventCursor current_event_cursor() noexcept {
    AcquireSRWLockShared(&g_lock);
    const EventCursor cursor{g_eventGeneration, g_sequence};
    ReleaseSRWLockShared(&g_lock);
    return cursor;
}

/** Copies retained events after one cursor and reports reset or overwrite gaps. */
void read_events_after(EventCursor after, EventRead& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    output.cursor = {g_eventGeneration, g_sequence};
    output.reset = after.generation != g_eventGeneration;
    if (g_eventCount != 0) {
        const std::uint64_t afterSequence = output.reset ? 0 : after.sequence;
        const std::uint64_t oldestSequence = g_events[g_eventStart].sequence;
        std::size_t first = 0;
        if (afterSequence < oldestSequence) {
            const std::uint64_t retainedPredecessor = oldestSequence - 1;
            if (afterSequence < retainedPredecessor) {
                output.gap = true;
                output.missed = retainedPredecessor - afterSequence;
            }
        } else {
            while (first < g_eventCount
                   && g_events[(g_eventStart + first) % g_events.size()].sequence
                          <= afterSequence) {
                ++first;
            }
        }
        for (; first < g_eventCount; ++first) {
            output.events[output.count++] = g_events[(g_eventStart + first) % g_events.size()];
        }
    }
    ReleaseSRWLockShared(&g_lock);
}

/** Reads the current accepted-mission-input position without replaying retained history. */
MissionInputCursor current_mission_input_cursor() noexcept {
    AcquireSRWLockShared(&g_lock);
    const MissionInputCursor cursor{g_eventGeneration, g_missionInputSequence};
    ReleaseSRWLockShared(&g_lock);
    return cursor;
}

/** @return True when durable mission State still owes this retained accepted row. */
[[nodiscard]] bool mission_input_owed(const MissionInputRecord& record) noexcept {
    state::activity::mission::InputSequenceSnapshot cursors{};
    // A faulted binding owes nothing. Its program is skipped, so it never commits, and its rows
    // would otherwise be retained for the life of the process.
    if (!state::activity::mission::input_sequence_snapshot(record.view.event.binding, cursors)
        || cursors.faulted) {
        return false;
    }
    const std::uint64_t sequence = record.view.event.missionSequence;
    return sequence > cursors.committed && sequence <= cursors.issued;
}

/** Drops every retained row that durable mission State no longer owes. */
void retire_settled_mission_inputs() noexcept {
    static_cast<void>(std::erase_if(g_missionInputs, [](const MissionInputRecord& record) noexcept {
        return !mission_input_owed(record);
    }));
}

/** Copies accepted client mission inputs after one cursor. */
void read_mission_inputs_after(MissionInputCursor after, MissionInputRead& output) noexcept {
    output = {};
    AcquireSRWLockExclusive(&g_lock);
    output.reset = after.generation != g_eventGeneration;
    const std::uint64_t afterSequence = output.reset ? 0 : after.sequence;
    retire_settled_mission_inputs();
    output.cursor = {g_eventGeneration, afterSequence};
    if (!g_missionInputs.empty()) {
        const std::uint64_t retainedPredecessor = g_missionInputs.front().view.sequence - 1;
        if (afterSequence < retainedPredecessor) {
            output.gap = true;
            output.missed = retainedPredecessor - afterSequence;
        }
        for (const MissionInputRecord& record : g_missionInputs) {
            if (record.view.sequence <= afterSequence) {
                continue;
            }
            if (output.count == output.events.size()) {
                break;
            }
            output.events[output.count++] = record.view;
        }
        if (output.count != 0) {
            output.cursor.sequence = output.events[output.count - 1].sequence;
        }
    } else if (afterSequence < g_missionInputSequence) {
        output.gap = true;
        output.missed = g_missionInputSequence - afterSequence;
        output.cursor.sequence = g_missionInputSequence;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Copies the exact Sense values owned by one retained accepted-input row. */
bool mission_input_sense_snapshot(std::uint64_t sequence,
                                  SenseObservationSnapshot& output) noexcept {
    namespace sense = middleware::bap::activity_message::sense_update;
    output = {};
    if (sequence == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    const MissionInputRecord* selected = nullptr;
    for (std::size_t offset = g_missionInputs.size(); offset != 0; --offset) {
        const MissionInputRecord& candidate = g_missionInputs[offset - 1];
        if (candidate.view.sequence == sequence) {
            selected = &candidate;
            break;
        }
    }
    bool copied = selected != nullptr && selected->hasSense
                  && selected->view.event.kind == EventKind::senseUpdate;
    if (copied) {
        const Event& event = selected->view.event;
        const sense::DecodedPacket& packet = selected->sense;
        copied = sense::observation_packet(packet);
        if (copied) {
            output.revision = event.sequence;
            output.sourceGeneration = event.sourceGeneration;
        }
        for (std::size_t index = 0; copied && index < packet.objectCount; ++index) {
            const sense::DecodedObject& object = packet.objects[index];
            if (object.status != sense::ObjectStatus::decoded || !object.hasGeneration) continue;
            if (object.firstValue > packet.valueCount
                || object.valueCount > packet.valueCount - object.firstValue) {
                copied = false;
                break;
            }
            SenseObservation observation{};
            observation.binding = event.binding;
            observation.key = {object.registryKey,
                               object.objectTag,
                               object.senseSchema,
                               object.schemaRow,
                               object.slotIndex,
                               object.slotType};
            observation.sequence = event.sequence;
            observation.tick = event.tick;
            observation.sourceGeneration = event.sourceGeneration;
            observation.clientMessageSequence = event.clientMessageSequence;
            observation.generationPlusOne = object.generationPlusOne;
            observation.hasGeneration = object.hasGeneration;
            copied = append_sense_observation(
                output, observation, {packet.values.data() + object.firstValue, object.valueCount});
        }
    }
    if (!copied) {
        output = {};
    }
    ReleaseSRWLockShared(&g_lock);
    return copied;
}

/** Copies one exact generic client-message snapshot owned by the mission-input feed. */
bool mission_input_client_message_snapshot(std::uint64_t sequence,
                                           ClientMessageSnapshot& output) noexcept {
    output = {};
    if (sequence == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    const MissionInputRecord* selected = nullptr;
    for (std::size_t offset = g_missionInputs.size(); offset != 0; --offset) {
        const MissionInputRecord& candidate = g_missionInputs[offset - 1];
        if (candidate.view.sequence == sequence) {
            selected = &candidate;
            break;
        }
    }
    const bool copied = selected != nullptr && selected->hasClientMessage
                        && selected->view.event.kind == EventKind::clientMessageReceived;
    if (copied) {
        output = selected->clientMessage;
    }
    ReleaseSRWLockShared(&g_lock);
    return copied;
}

/** Copies the latest complete Sense observations for one exact activity generation. */
bool snapshot_sense_observations(const state::activity::SessionBinding& binding,
                                 SenseObservationSnapshot& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    const Instance* const instance = find_instance(binding);
    const bool found = instance != nullptr;
    if (found) {
        output = instance->senseObservations;
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

/** Selects one exact reflected scalar without changing the retained snapshot. */
SenseScalarStatus select_sense_scalar(const SenseObservationSnapshot& snapshot,
                                      const state::activity::SessionBinding& binding,
                                      const SenseScalarIdentity& identity,
                                      SenseScalarSample& output) noexcept {
    namespace sense = middleware::bap::activity_message::sense_update;
    output = {};
    if (binding.sessionId == state::activity::kAbsentSessionId
        || binding.createdRevision == state::activity::kInvalidRevision
        || identity.object.schemaRow == sense::kAbsentRuntimeRow
        || identity.fieldSchemaRow == sense::kAbsentRuntimeRow
        || identity.fieldRow == sense::kAbsentRuntimeRow
        || identity.fieldSchemaRow != identity.object.schemaRow) {
        return SenseScalarStatus::invalidIdentity;
    }
    if (snapshot.sourceGeneration == 0 || snapshot.observationCount > snapshot.observations.size()
        || snapshot.valueCount > snapshot.values.size()) {
        return SenseScalarStatus::invalidSnapshot;
    }
    const auto sameKey = [&identity](const SenseObservationKey& candidate) noexcept {
        const SenseObservationKey& expected = identity.object;
        return candidate.registryKey == expected.registryKey
               && candidate.objectTag == expected.objectTag
               && candidate.senseSchema == expected.senseSchema
               && candidate.schemaRow == expected.schemaRow
               && candidate.slotIndex == expected.slotIndex
               && candidate.slotType == expected.slotType;
    };
    bool found = false;
    for (std::size_t observationIndex = 0; observationIndex < snapshot.observationCount;
         ++observationIndex) {
        const SenseObservation& observation = snapshot.observations[observationIndex];
        if (!same_binding(observation.binding, binding) || !sameKey(observation.key)) {
            continue;
        }
        if (observation.sourceGeneration != snapshot.sourceGeneration
            || observation.firstValue > snapshot.valueCount
            || observation.valueCount > snapshot.valueCount - observation.firstValue) {
            output = {};
            return SenseScalarStatus::invalidSnapshot;
        }
        for (std::size_t valueIndex = 0; valueIndex < observation.valueCount; ++valueIndex) {
            const sense::DecodedValue& value = snapshot.values[observation.firstValue + valueIndex];
            if (value.schemaRow != identity.fieldSchemaRow || value.fieldRow != identity.fieldRow
                || value.fieldOrdinal != identity.fieldOrdinal
                || value.occurrence != identity.occurrence || value.kind != identity.kind) {
                continue;
            }
            if (found) {
                output = {};
                return SenseScalarStatus::ambiguous;
            }
            output.binding = observation.binding;
            output.identity = identity;
            output.value = value;
            output.observationRevision = observation.sequence;
            output.tick = observation.tick;
            output.sourceGeneration = observation.sourceGeneration;
            output.clientMessageSequence = observation.clientMessageSequence;
            output.generationPlusOne = observation.generationPlusOne;
            output.hasGeneration = observation.hasGeneration;
            found = true;
        }
    }
    return found ? SenseScalarStatus::ready : SenseScalarStatus::notFound;
}

/** @return True when two selected scalars have the same presence and raw typed value. */
bool same_sense_scalar_value(const SenseScalarSample& left,
                             const SenseScalarSample& right) noexcept {
    if (left.value.kind != right.value.kind || left.value.present != right.value.present) {
        return false;
    }
    if (!left.value.present) {
        return true;
    }
    return left.value.unsignedValue == right.value.unsignedValue
           && left.value.signedValue == right.value.signedValue
           && std::bit_cast<std::uint32_t>(left.value.realValue)
                  == std::bit_cast<std::uint32_t>(right.value.realValue);
}

/** @return True when both samples name the same ActivityClient and reported object generations. */
bool same_sense_scalar_generations(const SenseScalarSample& left,
                                   const SenseScalarSample& right) noexcept {
    return left.sourceGeneration != 0 && left.sourceGeneration == right.sourceGeneration
           && left.hasGeneration && right.hasGeneration
           && left.generationPlusOne == right.generationPlusOne;
}

/** @return Stable text for one exact scalar-selection result. */
const char* sense_scalar_status_name(SenseScalarStatus status) noexcept {
    switch (status) {
    case SenseScalarStatus::ready:
        return "ready";
    case SenseScalarStatus::invalidIdentity:
        return "invalid_identity";
    case SenseScalarStatus::invalidSnapshot:
        return "invalid_snapshot";
    case SenseScalarStatus::notFound:
        return "not_found";
    case SenseScalarStatus::ambiguous:
        return "ambiguous";
    }
    return "invalid_status";
}

/** Reads the committed Auth state for one exact activity generation. */
bool auth_state(const state::activity::SessionBinding& binding, AuthState& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    const Instance* const instance = find_instance(binding);
    const bool found = instance != nullptr;
    if (found) {
        output.revision = instance->view.stateRevision;
        output.lifetimeState = instance->view.lifetimeState;
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

/** Reads the one pending incident occupying the exact instance's serialized output slot. */
bool pending_incident(const state::activity::SessionBinding& binding,
                      PendingIncident& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    const Instance* const instance = find_instance(binding);
    const bool pending = instance != nullptr && instance->view.active
                         && instance->view.outputPending
                         && instance->view.outputKind == OutputKind::incident;
    if (pending) {
        const IncidentRecord* const record =
            find_incident(binding, instance->view.incidentRevision);
        if (record != nullptr && record->status != IncidentStatus::canceled) {
            output.incident = record->incident;
            output.revision = record->revision;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return pending && output.revision != 0;
}

/** Cancels the exact instance's raw incident before any later transport staging. */
bool cancel_pending_incident(const state::activity::SessionBinding& binding) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    const bool canceled = instance != nullptr && instance->view.outputPending
                          && instance->view.outputKind == OutputKind::incident;
    if (canceled) {
        cancel_output(*instance, GetTickCount64());
    }
    ReleaseSRWLockExclusive(&g_lock);
    return canceled;
}

/** Records one refused BAP attempt without clearing the committed output. */
void note_auth_attempt(const state::activity::SessionBinding& binding,
                       std::uint64_t sourceGeneration,
                       std::uint64_t revision,
                       std::uint8_t lifetimeState,
                       OutputStatus status) noexcept {
    if (revision == 0 || status == OutputStatus::idle || status == OutputStatus::pending
        || status == OutputStatus::transportStaged || status == OutputStatus::canceled) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    if (instance != nullptr && instance->view.outputPending
        && instance->view.outputKind == OutputKind::authState
        && instance->view.stateRevision == revision
        && instance->view.lifetimeState == lifetimeState) {
        instance->view.lastOutputAttemptTick = GetTickCount64();
        instance->view.lastOutputSourceGeneration = sourceGeneration;
        ++instance->view.outputAttempts;
        instance->view.outputStatus = status;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Marks one committed Auth revision as staged into the transport output queue. */
void note_auth_transport_staged(const state::activity::SessionBinding& binding,
                                std::uint64_t sourceGeneration,
                                std::uint64_t revision,
                                std::uint8_t lifetimeState) noexcept {
    if (revision == 0) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    if (instance != nullptr && instance->view.outputPending
        && instance->view.outputKind == OutputKind::authState
        && instance->view.stateRevision == revision
        && instance->view.lifetimeState == lifetimeState) {
        instance->view.transportRevision = revision;
        instance->view.lastOutputAttemptTick = GetTickCount64();
        instance->view.lastOutputSourceGeneration = sourceGeneration;
        ++instance->view.outputAttempts;
        instance->view.outputStatus = OutputStatus::transportStaged;
        instance->view.outputPending = false;
        instance->view.outputKind = OutputKind::none;
        Event event{};
        event.binding = binding;
        event.tick = instance->view.lastOutputAttemptTick;
        event.kind = EventKind::authStateTransportStaged;
        event.sourceGeneration = sourceGeneration;
        event.stateRevision = revision;
        event.lifetimeState = lifetimeState;
        append_event(event);
        instance->view.lastEventSequence = g_sequence;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Records one failed incident attempt without clearing the serialized output slot. */
void note_incident_attempt(const state::activity::SessionBinding& binding,
                           std::uint64_t sourceGeneration,
                           std::uint64_t revision,
                           IncidentStatus status) noexcept {
    if (revision == 0
        || (status != IncidentStatus::encodeFailed && status != IncidentStatus::frameRefused)) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    IncidentRecord* const record = find_incident(binding, revision);
    if (instance != nullptr && record != nullptr && instance->view.outputPending
        && instance->view.outputKind == OutputKind::incident
        && instance->view.incidentRevision == revision) {
        record->lastAttemptTick = GetTickCount64();
        record->lastSourceGeneration = sourceGeneration;
        ++record->attempts;
        record->status = status;
        instance->view.lastOutputAttemptTick = record->lastAttemptTick;
        instance->view.lastOutputSourceGeneration = sourceGeneration;
        ++instance->view.outputAttempts;
        instance->view.outputStatus = OutputStatus::frameRefused;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Marks one retained incident revision as staged into a matching transport output queue. */
void note_incident_transport_staged(const state::activity::SessionBinding& binding,
                                    std::uint64_t sourceGeneration,
                                    std::uint64_t revision) noexcept {
    if (revision == 0) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    Instance* const instance = find_instance(binding);
    IncidentRecord* const record = find_incident(binding, revision);
    if (instance != nullptr && record != nullptr && instance->view.outputPending
        && instance->view.outputKind == OutputKind::incident
        && instance->view.incidentRevision == revision) {
        record->lastAttemptTick = GetTickCount64();
        record->lastSourceGeneration = sourceGeneration;
        ++record->attempts;
        ++record->transportStages;
        record->status = IncidentStatus::transportStaged;
        instance->view.incidentTransportRevision = revision;
        instance->view.lastOutputAttemptTick = record->lastAttemptTick;
        instance->view.lastOutputSourceGeneration = sourceGeneration;
        ++instance->view.outputAttempts;
        instance->view.outputStatus = OutputStatus::transportStaged;
        instance->view.outputPending = false;
        instance->view.outputKind = OutputKind::none;
        instance->view.incidentsPending = 0;
        Event event{};
        event.binding = binding;
        event.tick = record->lastAttemptTick;
        event.kind = EventKind::incidentTransportStaged;
        event.sourceGeneration = sourceGeneration;
        fill_incident_event(event, record->incident, revision);
        append_event(event);
        instance->view.lastEventSequence = g_sequence;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Clears every instance, queue, and diagnostic counter. */
void reset() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_eventGeneration = next_nonzero(g_eventGeneration);
    for (Instance& instance : g_instances) {
        clear_instance(instance);
    }
    std::vector<PendingInput>{}.swap(g_pending);
    for (Event& event : g_events) {
        event = {};
    }
    std::vector<MissionInputRecord>{}.swap(g_missionInputs);
    for (IncidentRecord& incident : g_incidents) {
        incident = {};
    }
    for (ClientMessageRecord& message : g_clientMessages) {
        message = {};
    }
    SecureZeroMemory(g_clientMessageDetails.data(), sizeof(g_clientMessageDetails));
    g_pendingRead = 0;
    g_queuedIngress = 0;
    g_queuedControls = 0;
    g_eventStart = 0;
    g_eventCount = 0;
    g_clientMessageStart = 0;
    g_clientMessageCount = 0;
    g_clientMessageDetailStart = 0;
    g_clientMessageDetailCount = 0;
    g_sequence = 0;
    g_scriptableReservationGeneration = next_nonzero(g_scriptableReservationGeneration);
    g_scriptableReservationSequence = 0;
    g_missionInputSequence = 0;
    g_clientMessageSequence = 0;
    g_touch = 0;
    g_droppedIngress = 0;
    g_droppedIncidents = 0;
    g_refusedControls = 0;
    g_refusedIncidents = 0;
    g_overwrittenEvents = 0;
    g_overwrittenIncidents = 0;
    g_overwrittenClientMessages = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace sunrise::server::activity::host
