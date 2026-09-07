#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "../../middleware/gameplay/external/actor_command_runtime_codec.h"
#include "../../middleware/gameplay/external/actor_entity_registry.h"
#include "../../middleware/gameplay/external/simulation_event_runtime_codec.h"
#include "../../state/activity/definition.h"
#include "../../state/activity_sdk/format.h"
#include "../../state/activity_sdk/runtime.h"
#include "group/group_host_sessions.h"
#include "peer/peer_transport.h"

namespace sunrise::server::gameplay::actor_command_policy {

/** One policy row per host group session, so every advertised host can carry one. */
inline constexpr std::size_t kSessionCapacity = group::kHostSessionCapacity;
/** SDK actor-class selections one squad can hold. Class indices are one byte wide. */
inline constexpr std::size_t kSelectedActorClassCapacity =
    (std::numeric_limits<std::uint8_t>::max)();
/** Exact authored squads one mission policy can select. */
inline constexpr std::size_t kSelectedSquadCapacity = 32;
/** Live squad entities retained from channel 2. */
inline constexpr std::size_t kSquadEntityCapacity = 64;
/** Actor tokens one live squad update can name. */
inline constexpr std::size_t kSquadActorCapacity = 80;
/** Queued commands one session can hold. One lane-0 write can carry the whole ledger. */
inline constexpr std::size_t kOutputCapacity =
    middleware::gameplay::external::kSimulationEventCapacity;
/** Damage restores one session can hold at once. */
inline constexpr std::size_t kReplayCapacity = 4;
/** Transmissions one command rides before it is dropped. */
inline constexpr std::uint8_t kMaximumAttempts = 4;

/** Where one output row sits between staging and its acknowledgement. */
enum class OutputState : std::uint8_t {
    empty,
    queued,
    inFlight,
};

/** Why one output row was queued, which decides its rollback. */
enum class OutputPurpose : std::uint8_t {
    policyCommand,
    restoreCommand,
    replay,
};

/** One encoded command waiting for, or riding, a lane-0 transmission. */
struct OutputRow final {
    middleware::gameplay::external::RuntimeEventDraft draft{};
    middleware::gameplay::external::EntityToken target{};
    std::uint64_t transmissionId{};
    std::uint32_t replayIndex{state::activity_sdk::format::kAbsentIndex};
    OutputState state{OutputState::empty};
    OutputPurpose purpose{OutputPurpose::policyCommand};
    std::uint8_t attempts{};
};

/** One retained event held behind its restore barrier. */
struct ReplayRow final {
    middleware::gameplay::external::EventReplayTransaction transaction{};
    middleware::gameplay::external::EntityToken target{};
    bool occupied{};
};

/** Authored ClientRef identity of one selected mission squad. */
struct SelectedSquad final {
    std::uint32_t registryKey{};
    std::uint32_t slotIndex{};
    std::uint8_t slotType{};
};

/** One live type-1 entity and the actor tokens in its latest update. */
struct SquadEntityRow final {
    middleware::gameplay::external::EntityToken token{};
    std::array<middleware::gameplay::external::EntityToken, kSquadActorCapacity> actors{};
    std::uint32_t registryKey{};
    std::uint32_t slotIndex{};
    std::uint8_t slotType{};
    std::uint8_t actorCount{};
    bool occupied{};
};

/** One group's durable policy and transport state. */
struct SessionRow final {
    state::activity::SessionBinding binding{};
    state::activity_sdk::Snapshot catalog{};
    middleware::gameplay::external::ActorEntityRegistry actors{};
    std::array<std::uint32_t, kSelectedActorClassCapacity> actorClasses{};
    std::array<SelectedSquad, kSelectedSquadCapacity> selectedSquads{};
    std::array<SquadEntityRow, kSquadEntityCapacity> squads{};
    std::array<OutputRow, kOutputCapacity> outputs{};
    std::array<ReplayRow, kReplayCapacity> replays{};
    std::uint64_t groupSessionId{};
    std::uint64_t hostGeneration{};
    peer::LinkIdentity linkIdentity{};
    std::uint32_t actorEventIndex{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t damageEventIndex{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t messageIndex{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t commandIndex{state::activity_sdk::format::kAbsentIndex};
    std::int32_t policyValue{};
    std::uint8_t actorClassCount{};
    std::uint8_t selectedSquadCount{};
    bool bindingRetained{};
    bool linkIdentityRetained{};
    bool policyActive{};
    bool occupied{};
};

/** Guards every field of every session row. */
extern SRWLOCK g_lock;
extern std::array<SessionRow, kSessionCapacity> g_sessions;
/** Monotonic service counter driving the replay barriers. Zero means no slice ran yet. */
extern std::uint64_t g_serviceFrame;

/** @return True when both tokens name the same live entity incarnation. */
[[nodiscard]] bool same_token(const middleware::gameplay::external::EntityToken& left,
                              const middleware::gameplay::external::EntityToken& right) noexcept;

/** @return The occupied row for one group session, or null. The caller holds the lock. */
[[nodiscard]] SessionRow* find_session(std::uint64_t groupSessionId) noexcept;

/** @return The row for one group session, allocating it when free. The caller holds the lock. */
[[nodiscard]] SessionRow* find_or_create_session(std::uint64_t groupSessionId) noexcept;

/**
 * Resolves one committed token to its SDK actor-class row.
 * @param session Row holding the registry and the selected classes.
 * @param target Token the caller decoded.
 * @param output Absent index, then the class row when the token is live.
 * @return True only when the token is live and its class is selected by the policy.
 */
[[nodiscard]] bool selected_entity_class(const SessionRow& session,
                                         const middleware::gameplay::external::EntityToken& target,
                                         std::uint32_t& output) noexcept;

/**
 * Encodes one command and queues it when the bounded output ledger can own it.
 * @param session Row owning the ledger and the SDK indices. The caller holds the lock.
 * @param actorClassIndex SDK actor class of the target.
 * @param target Live entity token the command names.
 * @param value Faction value the command carries.
 * @param purpose Decides how a rollback finds the row again.
 * @param replayIndex Replay row this command restores, or the absent index.
 * @return True only when the row is encoded and owned.
 */
[[nodiscard]] bool
queue_command(SessionRow& session,
              std::uint32_t actorClassIndex,
              const middleware::gameplay::external::EntityToken& target,
              std::int32_t value,
              OutputPurpose purpose,
              std::uint32_t replayIndex = state::activity_sdk::format::kAbsentIndex) noexcept;

/** Removes pending command and replay state for one retired token. */
void remove_target_state(SessionRow& session,
                         const middleware::gameplay::external::EntityToken& target) noexcept;

} // namespace sunrise::server::gameplay::actor_command_policy
