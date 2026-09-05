#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "../../../state/activity/transactions/internal.h"
#include "../activity_sdk_mission_runtime.h"
#include "mission_script_cinematic.h"
#include "mission_script_player_trigger.h"
#include "mission_script_runtime_internal.h"

// Sense and host-state edge derivation. The client publishes levels, so every edge one
// script sees is derived here by comparing the level against what this instance last saw.
// Each function reads one instance and pushes the events it derived back into it.

namespace sunrise::server::activity::mission {
namespace {

/** Trigger volume occupancy publishes on this Sense slot type. */
constexpr std::uint8_t kTriggerOccupancySlotType = 30;
/** Values in one occupancy body: any, all, intersection count, and copied Auth value. */
constexpr std::size_t kTriggerOccupancyValueCount = 4;
constexpr std::uint16_t kTriggerAnyOrdinal = 0;
constexpr std::uint16_t kTriggerAllOrdinal = 1;
constexpr std::uint16_t kTriggerCountOrdinal = 2;
constexpr std::uint16_t kTriggerThresholdOrdinal = 3;
/** Root ordinals of the two live squad fields. The rest of the root is inert on this build. */
constexpr std::uint16_t kSquadAliveOrdinal = 3;
constexpr std::uint16_t kSquadRemovalOrdinal = 8;
/** The per-slot counts are nested: a four-bit length, then the counts themselves. */
constexpr std::uint8_t kSquadSlotLengthWidth = 4;
constexpr std::uint8_t kSquadSlotCountWidth = 32;
/** Root ordinal of the authored scene's activation token. Signed, bias -2^31. */
constexpr std::uint16_t kSceneTokenOrdinal = 0;
/** Root ordinal of the authored scene's completion latch. A zero-width boolean. */
constexpr std::uint16_t kSceneCompletionOrdinal = 1;
/** Objective sensors publish on this Sense slot type. */
constexpr std::uint8_t kObjectiveSlotType = 3;
/** Objective sensor Sense root schema. */
constexpr std::uint32_t kObjectiveSenseSchema = 0x80807F04U;
/** The block's ordinal 1 is its one-bit client flag, which separates it from the task list. */
constexpr std::uint16_t kObjectiveBlockFlagOrdinal = 1;
constexpr std::uint8_t kObjectiveBlockFlagWidth = 1;
/** The client clamps a task counter here, so nothing above it is reachable. */
constexpr std::int32_t kObjectiveTaskCountLimit = 80;

namespace sense_values = middleware::bap::activity_message::sense_update;
namespace scenes = activity_sdk_mission;

/** @return The value one schema owns at one ordinal, or null when the body omits it. */
[[nodiscard]] const sense_values::DecodedValue*
sense_value(std::span<const sense_values::DecodedValue> body,
            std::uint32_t schemaRow,
            std::uint16_t ordinal) noexcept {
    for (const sense_values::DecodedValue& value : body) {
        if (value.schemaRow == schemaRow && value.fieldOrdinal == ordinal) {
            return &value;
        }
    }
    return nullptr;
}

/** @return The boolean at one ordinal. An omitted field asserts false. */
[[nodiscard]] bool sense_flag(std::span<const sense_values::DecodedValue> body,
                              std::uint32_t schemaRow,
                              std::uint16_t ordinal) noexcept {
    const sense_values::DecodedValue* const value = sense_value(body, schemaRow, ordinal);
    return value != nullptr && value->present && value->unsignedValue != 0;
}

/** @return The signed number at one ordinal. An omitted field asserts zero. */
[[nodiscard]] std::int32_t sense_number(std::span<const sense_values::DecodedValue> body,
                                        std::uint32_t schemaRow,
                                        std::uint16_t ordinal) noexcept {
    const sense_values::DecodedValue* const value = sense_value(body, schemaRow, ordinal);
    return value != nullptr && value->present ? static_cast<std::int32_t>(value->signedValue) : 0;
}

/** @return The identity every derived Sense edge carries. */
[[nodiscard]] host::Event sense_edge_event(const RuntimeInstance& instance,
                                           const host::SenseObservation& observation) noexcept {
    host::Event event{};
    event.binding = instance.view.binding;
    event.sequence = observation.sequence;
    event.tick = observation.tick;
    event.sourceGeneration = instance.view.activityClientGeneration;
    event.missionSequence = instance.lastMissionSequence;
    event.firstRegistryKey = observation.key.registryKey;
    event.firstSlotIndex = observation.key.slotIndex;
    event.firstSlotType = observation.key.slotType;
    event.slotObjectTag = observation.key.objectTag;
    event.slotSenseSchema = observation.key.senseSchema;
    return event;
}

/**
 * Copies the squad's per-slot counts out of its nested length-prefixed list.
 * The length and the counts sit in nested schemas, so they are the values the root does not own.
 * Their declared widths tell the length apart from the counts.
 * @return Live entries written into output.
 */
[[nodiscard]] std::uint8_t
squad_slot_counts(std::span<const sense_values::DecodedValue> body,
                  std::uint32_t rootSchemaRow,
                  std::array<std::int32_t, host::kSquadSlotCapacity>& output) noexcept {
    output = {};
    std::size_t length = 0;
    std::size_t written = 0;
    for (const sense_values::DecodedValue& value : body) {
        if (value.schemaRow == rootSchemaRow || !value.present) {
            continue;
        }
        if (value.width == kSquadSlotLengthWidth) {
            length = static_cast<std::size_t>(value.unsignedValue);
        } else if (value.width == kSquadSlotCountWidth && written < output.size()) {
            output[written++] = static_cast<std::int32_t>(value.signedValue);
        }
    }
    return static_cast<std::uint8_t>((std::min)(written, length));
}

/** @return The retained record for one squad, allocating on a first observation. */
[[nodiscard]] SquadObservation* find_squad(RuntimeInstance& instance,
                                           const host::SenseObservationKey& key) noexcept {
    SquadObservation* spare = nullptr;
    for (SquadObservation& retained : instance.squadObservations) {
        if (retained.used && retained.registryKey == key.registryKey
            && retained.objectTag == key.objectTag && retained.slotIndex == key.slotIndex) {
            return &retained;
        }
        if (!retained.used && spare == nullptr) {
            spare = &retained;
        }
    }
    if (spare == nullptr) {
        log_line(core::log::Level::warn, &instance, "squad", "watch_capacity");
        return nullptr;
    }
    spare->registryKey = key.registryKey;
    spare->objectTag = key.objectTag;
    spare->slotIndex = key.slotIndex;
    return spare;
}

/** @return The retained record for one objective sensor, allocating on a first observation. */
[[nodiscard]] ObjectiveObservation* find_objective(RuntimeInstance& instance,
                                                   const host::SenseObservationKey& key) noexcept {
    ObjectiveObservation* spare = nullptr;
    for (ObjectiveObservation& retained : instance.objectiveObservations) {
        if (retained.used && retained.registryKey == key.registryKey
            && retained.objectTag == key.objectTag && retained.slotIndex == key.slotIndex) {
            return &retained;
        }
        if (!retained.used && spare == nullptr) {
            spare = &retained;
        }
    }
    if (spare == nullptr) {
        log_line(core::log::Level::warn, &instance, "objective", "watch_capacity");
        return nullptr;
    }
    spare->registryKey = key.registryKey;
    spare->objectTag = key.objectTag;
    spare->slotIndex = key.slotIndex;
    return spare;
}

/** One objective sensor's task counters and the exact set the body carried. */
struct ObjectiveCounters final {
    std::array<std::array<std::uint8_t, kObjectiveTaskCapacity>, kObjectiveCapacity> value{};
    std::array<std::array<bool, kObjectiveTaskCapacity>, kObjectiveCapacity> present{};
    std::uint8_t blocks{};
};

/**
 * Copies one objective sensor's task counters out of its two nested schemas.
 * The 24 blocks share one schema row, so their order is the only separator: a block opens with its
 * own ordinal 0, then its task list. An omitted counter is absent, because zero is a real value.
 */
void objective_task_counters(std::span<const sense_values::DecodedValue> body,
                             std::uint32_t rootSchemaRow,
                             ObjectiveCounters& output) noexcept {
    output = {};
    std::uint32_t blockSchemaRow = sense_values::kAbsentRuntimeRow;
    for (const sense_values::DecodedValue& value : body) {
        if (value.schemaRow != rootSchemaRow && value.fieldOrdinal == kObjectiveBlockFlagOrdinal
            && value.width == kObjectiveBlockFlagWidth) {
            blockSchemaRow = value.schemaRow;
            break;
        }
    }
    if (blockSchemaRow == sense_values::kAbsentRuntimeRow) {
        return;
    }
    std::size_t blocks = 0;
    std::size_t tasks = 0;
    for (const sense_values::DecodedValue& value : body) {
        if (value.schemaRow == rootSchemaRow) {
            continue;
        }
        if (value.schemaRow == blockSchemaRow) {
            if (value.fieldOrdinal == 0) {
                ++blocks;
                tasks = 0;
            }
            continue;
        }
        if (blocks == 0 || blocks > output.value.size() || tasks >= kObjectiveTaskCapacity) {
            continue;
        }
        if (value.present) {
            const std::int64_t raw = value.signedValue;
            const std::int64_t clamped =
                raw <= 0 ? 0 : (std::min)(raw, std::int64_t{kObjectiveTaskCountLimit});
            output.value[blocks - 1][tasks] = static_cast<std::uint8_t>(clamped);
            output.present[blocks - 1][tasks] = true;
        }
        ++tasks;
    }
    output.blocks = static_cast<std::uint8_t>((std::min)(blocks, output.value.size()));
}

/** @return True when one roster row is a committed peer of this instance's destination. */
[[nodiscard]] bool peer_session(const RuntimeInstance& instance,
                                const state::activity::SessionRosterRow& row) noexcept {
    return row.joined && row.binding.sessionId != instance.view.binding.sessionId
           && state::activity::transactions::same_destination(row.binding.destination,
                                                              instance.view.binding.destination);
}

/** Mirrors the watched roster into the VM so any callback can read the current peer set. */
void publish_peer_set(RuntimeInstance& instance) noexcept {
    std::array<lua_vm::PeerSession, state::activity::kSessionCapacity> peers{};
    std::size_t count = 0;
    for (const SessionRosterWatch& watched : instance.sessionRoster) {
        if (!watched.used) {
            continue;
        }
        peers[count] = {
            watched.sessionId, watched.createdRevision, watched.memberKey, watched.joinIdentity};
        ++count;
    }
    lua_vm::publish_peers(instance.vm, std::span(peers.data(), count));
}

} // namespace

/** Resolves a native player-trigger notification to its authored type-31 source. */
void push_player_trigger(RuntimeInstance& instance, const host::Event& incident) noexcept {
    if (!incident.hasPlayerTrigger) {
        return;
    }
    const state::build_data::scriptables::Snapshot* const world = instance.worldView.snapshot();
    if (world == nullptr) {
        return;
    }
    middleware::bap::activity_message::player_trigger_incident::Payload payload{};
    payload.registryKey = incident.playerTriggerRegistryKey;
    payload.slotType = incident.playerTriggerSlotType;
    payload.slotIndex = incident.playerTriggerSlotIndex;
    payload.resolvedObjectId = incident.playerTriggerResolvedObjectId;
    player_trigger::Source source{};
    const player_trigger::ResolveStatus status = player_trigger::resolve(*world, payload, source);
    if (status != player_trigger::ResolveStatus::ready) {
        log_line(core::log::Level::warn,
                 &instance,
                 "player_trigger",
                 status == player_trigger::ResolveStatus::ambiguous        ? "ambiguous"
                 : status == player_trigger::ResolveStatus::invalidCatalog ? "invalid_catalog"
                                                                           : "absent");
        return;
    }
    host::Event event = incident;
    event.kind = host::EventKind::playerTrigger;
    event.firstRegistryKey = source.registryKey;
    event.slotObjectTag = source.objectTag;
    event.firstSlotIndex = source.slotIndex;
    event.firstSlotType = source.slotType;
    event.slotSenseSchema = 0;
    event.playerTriggerRegistryKey = source.volumeRegistryKey;
    event.playerTriggerSlotType = source.volumeSlotType;
    event.playerTriggerSlotIndex = source.volumeSlotIndex;
    push_script_event(instance, event);
}

/** Resolves a native cinematic notification to its exact authored Type-6 source. */
void push_cinematic(RuntimeInstance& instance, const host::Event& incident) noexcept {
    if (!incident.hasCinematic) {
        return;
    }
    const state::build_data::scriptables::Snapshot* const world = instance.worldView.snapshot();
    if (world == nullptr) {
        return;
    }
    middleware::bap::activity_message::cinematic_incident::Payload target{};
    target.registryKey = incident.cinematicRegistryKey;
    target.slotType = incident.cinematicSlotType;
    target.slotIndex = incident.cinematicSlotIndex;
    target.runtimeObjectId = incident.cinematicRuntimeObjectId;
    target.eventValue = incident.cinematicEventValue;
    cinematic::Source source{};
    const cinematic::ResolveStatus status = cinematic::resolve(*world, target, source);
    std::array<char, 128> fields{};
    const int written = std::snprintf(fields.data(),
                                      fields.size(),
                                      "registry=%08x slot_type=%d slot_index=%d target=%u",
                                      target.registryKey,
                                      static_cast<int>(target.slotType),
                                      static_cast<int>(target.slotIndex),
                                      incident.incidentTarget);
    const std::string_view detail =
        written > 0
            ? std::string_view(fields.data(),
                               (std::min)(static_cast<std::size_t>(written), fields.size() - 1))
            : std::string_view{};
    if (status != cinematic::ResolveStatus::ready) {
        log_line(core::log::Level::warn,
                 &instance,
                 "cinematic",
                 status == cinematic::ResolveStatus::ambiguous        ? "ambiguous"
                 : status == cinematic::ResolveStatus::invalidCatalog ? "invalid_catalog"
                                                                      : "absent",
                 detail);
        return;
    }
    log_line(core::log::Level::info,
             &instance,
             "cinematic",
             incident.cinematicSignal
                     == middleware::bap::activity_message::cinematic_incident::Signal::started
                 ? "started"
                 : "terminated",
             detail);
    host::Event event = incident;
    event.kind = incident.cinematicSignal
                         == middleware::bap::activity_message::cinematic_incident::Signal::started
                     ? host::EventKind::cinematicStarted
                     : host::EventKind::cinematicTerminated;
    event.firstRegistryKey = source.registryKey;
    event.slotObjectTag = source.objectTag;
    event.firstSlotIndex = source.slotIndex;
    event.firstSlotType = source.slotType;
    event.slotSenseSchema = 0;
    push_script_event(instance, event);
}

/**
 * Raises one event per watched volume whose occupancy changed.
 * The client publishes occupancy as a level, so the edge is ours to derive. A volume seen for the
 * first time only records its level, because a first observation is not an entry.
 */
void push_trigger_edges(RuntimeInstance& instance,
                        const host::SenseObservationSnapshot& sense) noexcept {
    for (std::size_t index = 0; index < sense.observationCount; ++index) {
        const host::SenseObservation& observation = sense.observations[index];
        if (observation.key.slotType != kTriggerOccupancySlotType
            || observation.valueCount < kTriggerOccupancyValueCount
            || observation.firstValue + kTriggerOccupancyValueCount > sense.valueCount) {
            continue;
        }
        const std::span<const sense_values::DecodedValue> body(
            &sense.values[observation.firstValue], kTriggerOccupancyValueCount);
        const std::uint32_t root = observation.key.schemaRow;
        const bool occupied = sense_flag(body, root, kTriggerAnyOrdinal);
        TriggerOccupancy* slot = nullptr;
        TriggerOccupancy* spare = nullptr;
        for (TriggerOccupancy& retained : instance.triggerOccupancy) {
            if (retained.used && retained.registryKey == observation.key.registryKey
                && retained.objectTag == observation.key.objectTag
                && retained.slotIndex == observation.key.slotIndex) {
                slot = &retained;
                break;
            }
            if (!retained.used && spare == nullptr) {
                spare = &retained;
            }
        }
        if (slot == nullptr) {
            if (spare == nullptr) {
                log_line(core::log::Level::warn, &instance, "trigger", "watch_capacity");
                continue;
            }
            spare->registryKey = observation.key.registryKey;
            spare->objectTag = observation.key.objectTag;
            spare->slotIndex = observation.key.slotIndex;
            spare->occupied = occupied;
            spare->used = true;
            continue;
        }
        if (slot->occupied == occupied) {
            continue;
        }
        slot->occupied = occupied;
        host::Event event = sense_edge_event(instance, observation);
        event.triggerAll = sense_flag(body, root, kTriggerAllOrdinal);
        event.triggerCount = sense_number(body, root, kTriggerCountOrdinal);
        event.triggerValue = sense_number(body, root, kTriggerThresholdOrdinal);
        event.kind = occupied ? host::EventKind::triggerEntered : host::EventKind::triggerExited;
        push_script_event(instance, event);
    }
}

/**
 * Raises the squad events derived from one msg 6 body.
 * The client publishes levels, so a first sighting only records them. A slot count that rose is the
 * only spawn signal, and the alive count falling is the only death signal.
 */
void push_squad_edges(RuntimeInstance& instance,
                      const host::SenseObservationSnapshot& sense) noexcept {
    for (std::size_t index = 0; index < sense.observationCount; ++index) {
        const host::SenseObservation& observation = sense.observations[index];
        if (observation.key.senseSchema != format::kSquadSenseSchema
            || observation.firstValue + observation.valueCount > sense.valueCount) {
            continue;
        }
        const std::span<const sense_values::DecodedValue> body(
            &sense.values[observation.firstValue], observation.valueCount);
        const std::uint32_t root = observation.key.schemaRow;
        // An unchanged squad sends an empty delta, which must not read as a squad of zero.
        if (sense_value(body, root, kSquadAliveOrdinal) == nullptr) {
            continue;
        }
        const std::int32_t alive = sense_number(body, root, kSquadAliveOrdinal);
        const bool removal = sense_flag(body, root, kSquadRemovalOrdinal);
        std::array<std::int32_t, host::kSquadSlotCapacity> counts{};
        const std::uint8_t countLength = squad_slot_counts(body, root, counts);

        SquadObservation* const squad = find_squad(instance, observation.key);
        if (squad == nullptr) {
            continue;
        }
        const bool first = !squad->used;
        squad->used = true;
        const std::int32_t previousAlive = first ? 0 : squad->aliveCount;
        const bool changed = first || squad->aliveCount != alive || squad->removalFlag != removal
                             || squad->slotCountLength != countLength
                             || squad->slotCounts != counts;
        if (!changed) {
            continue;
        }
        host::Event state = sense_edge_event(instance, observation);
        state.squadAliveCount = alive;
        state.squadPreviousAliveCount = previousAlive;
        state.squadRemovalFlag = removal;
        state.squadSlotCounts = counts;
        state.squadSlotCountLength = countLength;
        state.kind = host::EventKind::squadState;
        if (instance.programStatus == ProgramStatus::loaded) {
            push_script_event(instance, state);
        }

        for (std::uint8_t slot = 0; slot < countLength; ++slot) {
            const std::int32_t previous =
                !first && slot < squad->slotCountLength ? squad->slotCounts[slot] : 0;
            if (counts[slot] <= previous) {
                continue;
            }
            host::Event spawned = sense_edge_event(instance, observation);
            spawned.squadSlotOrdinal = slot;
            spawned.squadSlotValue = counts[slot];
            spawned.squadPreviousSlotValue = previous;
            spawned.kind = host::EventKind::entitySpawned;
            if (instance.programStatus == ProgramStatus::loaded) {
                push_script_event(instance, spawned);
            }
        }
        if (!first && alive < squad->aliveCount) {
            host::Event died = sense_edge_event(instance, observation);
            died.squadAliveCount = alive;
            died.squadPreviousAliveCount = squad->aliveCount;
            died.kind = host::EventKind::entityDied;
            if (instance.programStatus == ProgramStatus::loaded) {
                push_script_event(instance, died);
            }
        }
        squad->aliveCount = alive;
        squad->removalFlag = removal;
        squad->slotCounts = counts;
        squad->slotCountLength = countLength;
    }
}

/**
 * Raises one event per watched authored scene that latched complete.
 * The completion field is a latch, so only the false to true edge is a finish. A scene seen for
 * the first time records its level and raises nothing.
 */
void push_scene_edges(RuntimeInstance& instance,
                      const host::SenseObservationSnapshot& sense) noexcept {
    for (std::size_t index = 0; index < sense.observationCount; ++index) {
        const host::SenseObservation& observation = sense.observations[index];
        if (observation.key.slotType != format::kAuthoredSceneSlotType
            || observation.key.senseSchema != format::kAuthoredSceneSenseSchema
            || observation.firstValue + observation.valueCount > sense.valueCount) {
            continue;
        }
        const std::span<const sense_values::DecodedValue> body(
            &sense.values[observation.firstValue], observation.valueCount);
        const std::uint32_t root = observation.key.schemaRow;
        // An unchanged scene sends an empty delta, which must not read as a completion of false.
        if (sense_value(body, root, kSceneCompletionOrdinal) == nullptr) {
            continue;
        }
        const bool completed = sense_flag(body, root, kSceneCompletionOrdinal);
        SceneObservation* slot = nullptr;
        SceneObservation* spare = nullptr;
        for (SceneObservation& retained : instance.sceneObservations) {
            if (retained.used && retained.registryKey == observation.key.registryKey
                && retained.objectTag == observation.key.objectTag
                && retained.slotIndex == observation.key.slotIndex) {
                slot = &retained;
                break;
            }
            if (!retained.used && spare == nullptr) {
                spare = &retained;
            }
        }
        if (slot == nullptr) {
            if (spare == nullptr) {
                log_line(core::log::Level::warn, &instance, "scene", "watch_capacity");
                continue;
            }
            spare->registryKey = observation.key.registryKey;
            spare->objectTag = observation.key.objectTag;
            spare->slotIndex = observation.key.slotIndex;
            spare->completed = completed;
            spare->used = true;
            continue;
        }
        const bool rising = completed && !slot->completed;
        slot->completed = completed;
        if (!rising) {
            continue;
        }
        host::Event event = sense_edge_event(instance, observation);
        event.sceneActivationToken = sense_number(body, root, kSceneTokenOrdinal);
        event.kind = host::EventKind::sceneFinished;
        push_script_event(instance, event);
    }
}

/**
 * Raises one event per watched objective task counter that rose.
 * The client owns the counter and raises it on an actor teardown, so a rise is the only progress
 * signal. A sensor seen for the first time records its counters and raises nothing.
 */
void push_objective_edges(RuntimeInstance& instance,
                          const host::SenseObservationSnapshot& sense) noexcept {
    for (std::size_t index = 0; index < sense.observationCount; ++index) {
        const host::SenseObservation& observation = sense.observations[index];
        if (observation.key.slotType != kObjectiveSlotType
            || observation.key.senseSchema != kObjectiveSenseSchema
            || observation.firstValue + observation.valueCount > sense.valueCount) {
            continue;
        }
        const std::span<const sense_values::DecodedValue> body(
            &sense.values[observation.firstValue], observation.valueCount);
        ObjectiveCounters counters{};
        objective_task_counters(body, observation.key.schemaRow, counters);
        // An unchanged sensor sends an empty delta, which must not read as every counter at zero.
        if (counters.blocks == 0) {
            continue;
        }
        ObjectiveObservation* const watched = find_objective(instance, observation.key);
        if (watched == nullptr) {
            continue;
        }
        const bool first = !watched->used;
        watched->used = true;
        for (std::uint8_t block = 0; block < counters.blocks; ++block) {
            for (std::size_t task = 0; task < kObjectiveTaskCapacity; ++task) {
                if (!counters.present[block][task]) {
                    continue;
                }
                const std::uint8_t value = counters.value[block][task];
                const std::uint8_t previous = watched->counters[block][task];
                watched->counters[block][task] = value;
                if (first || value <= previous) {
                    continue;
                }
                host::Event event = sense_edge_event(instance, observation);
                event.objectiveOrdinal = block;
                event.objectiveTaskOrdinal = static_cast<std::uint8_t>(task);
                event.objectiveTaskCount = value;
                event.objectivePreviousTaskCount = previous;
                event.kind = host::EventKind::objectiveProgress;
                push_script_event(instance, event);
            }
        }
    }
}

/** Reports one committed phase change to the script. */
void queue_phase_entered(RuntimeInstance& instance, std::uint32_t previousPhase) noexcept {
    host::Event event{};
    event.binding = instance.view.binding;
    event.sequence = instance.missionStateRevision;
    event.sourceGeneration = instance.view.activityClientGeneration;
    event.missionSequence = instance.lastMissionSequence;
    event.stateRevision = instance.missionStateRevision;
    event.missionPhase = instance.missionPhase;
    event.previousMissionPhase = previousPhase;
    event.kind = host::EventKind::phaseEntered;
    push_script_event(instance, event);
}

/** @return The identity every host-state edge that is not a Sense edge carries. */
[[nodiscard]] host::Event state_edge_event(const RuntimeInstance& instance) noexcept {
    host::Event event{};
    event.binding = instance.view.binding;
    event.sequence = instance.missionStateRevision;
    event.sourceGeneration = instance.view.activityClientGeneration;
    event.missionSequence = instance.lastMissionSequence;
    return event;
}

/**
 * Raises one event per peer session that appeared or left this instance's destination.
 * A record replacement changes createdRevision, which counts as a leave and a join. The first read
 * only records the current peers, so a reattach never replays them.
 */
void push_session_roster_edges(RuntimeInstance& instance,
                               std::span<const state::activity::SessionRosterRow> roster) noexcept {
    const bool first = !instance.sessionRosterObserved;
    instance.sessionRosterObserved = true;
    for (SessionRosterWatch& watched : instance.sessionRoster) {
        if (!watched.used) {
            continue;
        }
        const auto match = std::find_if(
            roster.begin(), roster.end(), [&instance, &watched](const auto& row) noexcept {
                return peer_session(instance, row) && row.binding.sessionId == watched.sessionId
                       && row.binding.createdRevision == watched.createdRevision;
            });
        if (match != roster.end()) {
            continue;
        }
        host::Event event = state_edge_event(instance);
        event.peerSessionId = watched.sessionId;
        event.peerSessionGeneration = watched.createdRevision;
        event.peerMemberKey = watched.memberKey;
        event.kind = host::EventKind::sessionLeft;
        watched = {};
        push_script_event(instance, event);
    }
    for (const state::activity::SessionRosterRow& row : roster) {
        if (!peer_session(instance, row)) {
            continue;
        }
        SessionRosterWatch* spare = nullptr;
        bool known = false;
        for (SessionRosterWatch& watched : instance.sessionRoster) {
            if (watched.used && watched.sessionId == row.binding.sessionId
                && watched.createdRevision == row.binding.createdRevision) {
                watched.memberKey = row.memberKey;
                watched.joinIdentity = row.joinIdentity;
                known = true;
                break;
            }
            if (!watched.used && spare == nullptr) {
                spare = &watched;
            }
        }
        if (known || spare == nullptr) {
            continue;
        }
        spare->sessionId = row.binding.sessionId;
        spare->createdRevision = row.binding.createdRevision;
        spare->memberKey = row.memberKey;
        spare->joinIdentity = row.joinIdentity;
        spare->used = true;
        if (first) {
            continue;
        }
        host::Event event = state_edge_event(instance);
        event.peerSessionId = row.binding.sessionId;
        event.peerSessionGeneration = row.binding.createdRevision;
        event.peerMemberKey = row.memberKey;
        event.stateRevision = row.joinedRevision;
        event.kind = host::EventKind::sessionJoined;
        push_script_event(instance, event);
    }
    publish_peer_set(instance);
}

} // namespace sunrise::server::activity::mission
