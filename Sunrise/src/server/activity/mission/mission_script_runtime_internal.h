#include "mission_script_squad_sense.h"
#include "mission_script_actor_path_sense.h"
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "../../../core/logging/log.h"
#include "../../../state/activity/mission/runtime.h"
#include "../../../state/activity/runtime.h"
#include "../../../state/activity_sdk/generated_world/runtime.h"
#include "../../../state/activity_sdk/runtime.h"
#include "../host_runtime.h"
#include "mission_script_vm.h"
#include "mission_script_ghost_sense.h"
#include "mission_script_object_sense.h"
#include "mission_script_player_sense.h"

// What the four mission-runtime translation units share. One owns the instance table and the
// service slice. One runs the delivery state machine. One fans one intent out to its adapter. One
// derives the Sense and host-state edges.

namespace sunrise::server::activity::mission {

namespace sdk = state::activity_sdk;
namespace generated = state::activity_sdk::generated_world;
namespace format = state::activity_sdk::format;
namespace mission_state = state::activity::mission;

enum class ProgramStatus : std::uint8_t {
    none,
    loaded,
    missing,
    fileError,
    sourceTooLarge,
    programError,
};

enum class DeliveryStage : std::uint8_t {
    idle,
    awaitingHostCommit,
    awaitingTransport,
    awaitingCancel,
};

/** Watched trigger volumes retained per instance. */
constexpr std::size_t kTriggerOccupancyCapacity = 32;
/** Watched squads retained per instance. A social destination places well over a hundred. */
constexpr std::size_t kSquadObservationCapacity = 160;
/** Watched authored scenes retained per instance. */
constexpr std::size_t kSceneObservationCapacity = 32;
/** Watched objective sensors retained per instance. */
constexpr std::size_t kObjectiveObservationCapacity = 32;
/** One objective sensor carries this many objective blocks. */
constexpr std::size_t kObjectiveCapacity = 24;
/** One objective block carries this many task counters. */
constexpr std::size_t kObjectiveTaskCapacity = 24;
/** How long a committed Host output may take to reach transport before the delivery faults. */
constexpr std::uint64_t kTransportTimeoutMs = 15'000;
/** How long an enqueued Host output may take to reach the reducer before the delivery retries. */
constexpr std::uint64_t kHostCommitTimeoutMs = 2'000;

/** Last occupancy seen for one watched volume, so only a change raises an event. */
struct TriggerOccupancy final {
    std::uint32_t registryKey{};
    std::uint32_t objectTag{};
    std::uint16_t slotIndex{};
    bool occupied{};
    bool used{};
};

/** Last squad counters seen for one watched object, so only a change raises an event. */
struct ActorPathObservation final {
    ActorPathLevel level{};
    std::uint32_t registryKey{}, objectTag{};
    std::uint16_t slotIndex{};
    bool used{};
};
struct DamageObservation final {
    std::uint32_t registryKey{}, objectTag{};
    std::uint16_t slotIndex{};
    std::int32_t revision{};
    float health{-1.0F}, shield{-1.0F};
    bool used{};
};
struct ObjectInteractionObservation final {
    ObjectInteractionLevel level{};
    std::uint32_t registryKey{}, objectTag{};
    std::uint16_t slotIndex{};
    bool used{};
};
struct GhostObservation final {
    GhostLevel level{};
    std::uint32_t registryKey{};
    std::uint32_t objectTag{};
    std::uint16_t slotIndex{};
    bool used{};
};
struct SquadObservation final {
    SquadObjectiveCosts objectiveCosts{};
    std::array<std::int32_t, host::kSquadSlotCapacity> slotCounts{};
    std::uint32_t registryKey{};
    std::uint32_t objectTag{};
    std::int32_t aliveCount{};
    std::uint16_t slotIndex{};
    std::uint8_t slotCountLength{};
    bool removalFlag{};
    bool used{};
};
/** Last completion latch seen for one watched authored scene, so only the edge raises an event. */
struct SceneObservation final {
    std::uint32_t registryKey{};
    std::uint32_t objectTag{};
    std::uint16_t slotIndex{};
    bool completed{};
    bool used{};
};

/** Last task counters seen for one watched objective sensor, so only a rise raises an event. */
struct ObjectiveObservation final {
    std::array<std::array<std::uint8_t, kObjectiveTaskCapacity>, kObjectiveCapacity> counters{};
    std::uint32_t registryKey{};
    std::uint32_t objectTag{};
    std::uint16_t slotIndex{};
    bool used{};
};

/** Last peer session seen sharing this destination, so only a change raises an event. */
struct SessionRosterWatch final {
    std::uint64_t sessionId{};
    std::uint64_t createdRevision{};
    std::uint64_t memberKey{};
    std::uint64_t joinIdentity{};
    bool used{};
};

/** Everything one bound mission program owns: its VM, views, delivery state and counters. */
struct RuntimeInstance final {
    lua_vm::Vm vm{};
    sdk::BoundView view{};
    generated::GeneratedWorldView worldView{};
    lua_vm::ProgramIdentity identity{};
    mission_state::ProgramKey programKey{};
    std::array<char, 16> lastVmStage{};
    std::array<char, 32> lastVmStatus{};
    std::uint64_t eventsSeen{};
    std::uint64_t eventsCommitted{};
    std::uint64_t intentsTransportStaged{};
    std::uint64_t lastEventSequence{};
    std::uint64_t lastLoggedRevision{};
    std::uint64_t lastMissionSequence{};
    std::uint64_t missionStateRevision{};
    std::uint32_t missionPhase{};
    std::uint64_t activityStateRevision{};
    std::uint64_t durableIntentSequence{};
    std::uint64_t durableHostOutputRevision{};
    std::uint64_t expectedScriptableRevision{};
    std::uint64_t deliveryDeadline{};
    std::uint64_t firstIntentAttempt{};
    std::uint64_t nextIntentAttempt{};
    std::uint64_t firstStartAttempt{};
    std::uint64_t nextStartAttempt{};
    host::Event pendingTimerEvent{};
    std::uint64_t firstTimerAttempt{};
    std::uint64_t nextTimerAttempt{};
    std::array<TriggerOccupancy, kTriggerOccupancyCapacity> triggerOccupancy{};
    std::array<SquadObservation, kSquadObservationCapacity> squadObservations{};
    std::array<GhostObservation, 8> ghostObservations{};
    std::array<DamageObservation, 8> damageObservations{};
    std::array<ObjectInteractionObservation, 64> objectInteractionObservations{};
    std::array<ActorPathObservation, 64> actorPathObservations{};
    std::array<SceneObservation, kSceneObservationCapacity> sceneObservations{};
    std::array<ObjectiveObservation, kObjectiveObservationCapacity> objectiveObservations{};
    // The table is exactly as large as the session table, so it can never overflow.
    std::array<SessionRosterWatch, state::activity::kSessionCapacity> sessionRoster{};
    std::array<PlayerLifeObservation, 16> playerLife{};
    std::uint64_t playerLifeGeneration{};
    FireteamLife lastFireteamLife{};
    bool fireteamLifePublished{};
    /** Dynamically sized host-state reports waiting for this script, in arrival order. */
    std::vector<host::Event> scriptEvents{};
    std::uint64_t firstScriptEventAttempt{};
    std::uint64_t nextScriptEventAttempt{};
    std::uint32_t intentAttempts{};
    std::uint32_t startAttempts{};
    std::uint32_t timerAttempts{};
    std::uint32_t scriptEventAttempts{};
    std::size_t durablePendingIntentCount{};
    std::size_t scriptEventRead{};
    std::uint16_t lastIntentStatus{(std::numeric_limits<std::uint16_t>::max)()};
    std::int32_t initialStateRegion{-1};
    /** Last client-reported activity region used to select state-local incoming references. */
    std::int32_t activeRegion{-1};
    ProgramStatus programStatus{ProgramStatus::none};
    DeliveryStage deliveryStage{DeliveryStage::idle};
    /** The player key the bound link's message 5 binds, read at attach. */
    std::uint64_t playerKey{};
    /** Bounded diagnostic budget for Ember bridge and console Sense fields. */
    std::uint16_t emberInteractionReports{};
    std::array<std::uint64_t, 64> emberInteractionValues{};
    std::uint64_t emberInteractionSeen{};
    bool publicTarget{};
    bool missionStateBound{};
    bool missionStarted{};
    bool missionStateFaulted{};
    bool initialStateDeclared{};
    bool initialStateSelected{};
    bool startPending{};
    bool timerPending{};
    /** Set after the first roster read, so an attach never replays every existing peer. */
    bool sessionRosterObserved{};
    /** This VM reattached to its current session, so its entry is on_load, not on_start. */
    bool missionReattached{};
    bool occupied{};
};

/** Writes one bounded mission-script diagnostic line. Fields are key=value, error is free text. */
void log_line(core::log::Level level,
              const RuntimeInstance* instance,
              std::string_view stage,
              std::string_view result,
              std::string_view fields = {},
              std::string_view error = {}) noexcept;
/** Appends one host-state event for the script. */
void push_script_event(RuntimeInstance& instance, const host::Event& event) noexcept;

/** Raises one event per watched trigger volume whose occupancy changed. */
void push_trigger_edges(RuntimeInstance& instance,
                        const host::SenseObservationSnapshot& sense) noexcept;
/** Raises one dedicated type-31 edge from a decoded schema-0x8080879F msg-19 payload. */
void push_player_trigger(RuntimeInstance& instance, const host::Event& incident) noexcept;
/** Raises one exact Type-6 start/finish edge from a decoded schema-0x808087BF msg-19 payload. */
void push_cinematic(RuntimeInstance& instance, const host::Event& incident) noexcept;
void push_actor_path_edges(RuntimeInstance& instance, const host::SenseObservationSnapshot& sense) noexcept;
void push_damage_edges(RuntimeInstance& instance, const host::SenseObservationSnapshot& sense) noexcept;
void push_object_interaction_edges(RuntimeInstance& instance, const host::SenseObservationSnapshot& sense) noexcept;
void push_ghost_edges(RuntimeInstance& instance,
                      const host::SenseObservationSnapshot& sense) noexcept;
/** Raises the squad state, spawn and death events derived from one msg 6 body. */
void push_squad_edges(RuntimeInstance& instance,
                      const host::SenseObservationSnapshot& sense) noexcept;
/** Raises one event per watched authored scene that latched complete. */
void push_scene_edges(RuntimeInstance& instance,
                      const host::SenseObservationSnapshot& sense) noexcept;
/** Raises one event per watched objective task counter that rose. */
void push_objective_edges(RuntimeInstance& instance,
                          const host::SenseObservationSnapshot& sense) noexcept;
/** Reports one committed phase change to the script. */
void queue_phase_entered(RuntimeInstance& instance, std::uint32_t previousPhase) noexcept;
/** @return The identity every host-state edge that is not a Sense edge carries. */
[[nodiscard]] host::Event state_edge_event(const RuntimeInstance& instance) noexcept;
/** Raises one event per peer session that appeared or left this instance's destination. */
void push_session_roster_edges(RuntimeInstance& instance,
                               std::span<const state::activity::SessionRosterRow> roster) noexcept;

// The runtime unit owns these. The delivery state machine calls into them.

/** Retires every queued mission event that belongs to one binding. */
void clear_pending_events(const state::activity::SessionBinding& binding) noexcept;
/** Copies one committed authoritative snapshot into the runtime's exact compare baseline. */
void accept_mission_state(RuntimeInstance& instance,
                          const mission_state::Snapshot& snapshot) noexcept;
/** Marks the already-faulted VM in durable State when its exact compare still matches. */
void persist_mission_fault(RuntimeInstance& instance) noexcept;
/** Faults both the VM and the exact server-owned mission record. */
void fault_instance(RuntimeInstance& instance, std::string_view reason) noexcept;

// The delivery unit owns these. The runtime unit's service slice calls into them.

/** @return now plus delay, saturated at the maximum instead of wrapping. */
[[nodiscard]] std::uint64_t deadline_after(std::uint64_t now, std::uint64_t delay) noexcept;
/** Retires the head event after its single delivery attempt. */
void retire_script_event(RuntimeInstance& instance) noexcept;
/** Advances the delivery stage from one Host delivery lifecycle event. */
void observe_delivery_event(RuntimeInstance& instance,
                            const host::Event& event,
                            std::uint64_t now) noexcept;
/** Reconciles an output assigned before a terminal program fault. */
void reconcile_terminal_delivery(RuntimeInstance& instance) noexcept;

// The delivery unit owns these too. The intent fan-out drives the state machine through them.

/** Releases an exact unstaged Host revision while retaining the durable intent. */
[[nodiscard]] bool release_delivery_state(RuntimeInstance& instance) noexcept;
/** Returns the instance to the idle stage and clears delivery timing. */
void clear_delivery(RuntimeInstance& instance) noexcept;
/** Retires one successfully applied local effect without assigning a Host output revision. */
[[nodiscard]] bool complete_local_effect(RuntimeInstance& instance,
                                         std::string_view result) noexcept;
/** Faults the program and abandons the delivery. */
void fault_delivery(RuntimeInstance& instance,
                    std::string_view result,
                    std::string_view reason) noexcept;
/** Reports one refused request to the script and keeps the program running. */
void refuse_delivery(RuntimeInstance& instance,
                     std::string_view result,
                     std::string_view reason,
                     host::EffectOutcome outcome) noexcept;
/** Dedup keys for report_intent_status. Each pending wait owns one value in its own range. */
inline constexpr std::uint16_t kIntentStatusCancelPending = 0x2FF;
inline constexpr std::uint16_t kIntentStatusSceneOutputBusy = 0x301;
inline constexpr std::uint16_t kIntentStatusStateTransitionPending = 0x302;
inline constexpr std::uint16_t kIntentStatusSceneLeasePending = 0x303;
/** Logs one adapter status, and only when it differs from the last one logged. */
void report_intent_status(RuntimeInstance& instance,
                          std::uint16_t status,
                          std::string_view name) noexcept;
/** @return Whether the intent has been in delivery longer than its lifetime allows. */
[[nodiscard]] bool intent_lifetime_expired(const RuntimeInstance& instance,
                                           std::uint64_t now) noexcept;
/** @return Whether an exact transport stage was found, so the delivery completed here. */
[[nodiscard]] bool reconcile_transport_stage(RuntimeInstance& instance) noexcept;
/** @return Whether an expired delivery was completed or left owned, so the caller must stop. */
[[nodiscard]] bool reconcile_expired_delivery(RuntimeInstance& instance) noexcept;
/** @return Whether a stage deadline fired, so no further delivery work is owed this tick. */
[[nodiscard]] bool service_delivery_timeout(RuntimeInstance& instance, std::uint64_t now) noexcept;

// The dispatch unit owns this. The runtime unit's service slice calls it.

/** Raises one queued intent, or advances the delivery already in flight. */
void dispatch_intent(RuntimeInstance& instance, std::uint64_t now) noexcept;

} // namespace sunrise::server::activity::mission
