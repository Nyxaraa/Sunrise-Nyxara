#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace sunrise::state::gameplay::external {

/** Entity handles use a 13-bit slot index. */
inline constexpr std::size_t kReplicationEntityCapacity = 8192;
/** Entity handle incarnations use four bits. */
inline constexpr std::uint8_t kEntityIncarnationMask = 0x0F;
/** A pristine slot starts its first allocation at sequence 2. */
inline constexpr std::uint8_t kFirstAllocationSequence = 2;
/** Wrapped allocation and internal token sequences resume at one. */
inline constexpr std::uint8_t kFirstNonzeroSequence = 1;
/** A peer ledger starts with one nonzero lifecycle epoch. */
inline constexpr std::uint64_t kFirstLedgerEpoch = 1;

/** Stable wire identity of one allocated entity slot. */
struct EntityHandle {
    std::uint16_t slot{};
    std::uint8_t incarnation{};

    bool operator==(const EntityHandle&) const = default;
};

/** Opaque definition identities needed by the entity payload codec. */
struct EntityDefinitionIdentity {
    std::uint32_t entityType{};
    std::uint32_t rsat{};

    bool operator==(const EntityDefinitionIdentity&) const = default;
};

/** Immutable identity and revision captured for one contribution. */
struct EntitySnapshot {
    EntityHandle handle{};
    EntityDefinitionIdentity definition{};
    std::uint64_t revision{};
    std::uint8_t allocationSequence{};

    bool operator==(const EntitySnapshot&) const = default;
};

/** Owns the bounded server-side entity slots and their generations. */
class ReplicationWorldState {
public:
    /** Allocates one slot and returns its first publishable snapshot. */
    [[nodiscard]] std::optional<EntitySnapshot>
    allocate(EntityDefinitionIdentity definition) noexcept;

    /** Advances one live entity revision without interpreting its payload. */
    [[nodiscard]] std::optional<EntitySnapshot> revise(EntityHandle handle) noexcept;

    /** Frees one live entity and returns the removal tombstone. */
    [[nodiscard]] std::optional<EntitySnapshot> release(EntityHandle handle) noexcept;

    /** Returns the current live snapshot for an exact handle. */
    [[nodiscard]] std::optional<EntitySnapshot> snapshot(EntityHandle handle) const noexcept;

    /** Returns the live snapshot at one slot for late-join seeding. */
    [[nodiscard]] std::optional<EntitySnapshot> snapshot_at(std::uint16_t index) const noexcept;

    /** Returns the number of live entities. */
    [[nodiscard]] std::size_t size() const noexcept;

private:
    /** Persistent generation state for one bounded slot. */
    struct Slot {
        EntityDefinitionIdentity definition{};
        std::uint64_t revision{};
        std::uint8_t allocationSequence{};
        std::uint8_t incarnation{};
        bool occupied{};
    };

    /** Advances the nonzero world revision counter. */
    [[nodiscard]] std::uint64_t advance_revision() noexcept;

    std::array<Slot, kReplicationEntityCapacity> slots_{};
    std::array<std::uint16_t, kReplicationEntityCapacity> releasedSlots_{};
    std::size_t releasedCount_{};
    std::size_t nextPristineSlot_{};
    std::size_t liveCount_{};
    std::uint64_t revision_{};
};

/** Wire action owed by one peer for one entity slot. */
enum class ContributionKind : std::uint8_t {
    create,
    update,
    remove,
};

/** One packet-owned contribution token. */
struct ReplicationContribution {
    EntitySnapshot entity{};
    std::uint64_t ledgerEpoch{};
    std::uint64_t transmissionId{};
    ContributionKind kind{ContributionKind::create};

    bool operator==(const ReplicationContribution&) const = default;
};

/** Read-only state for one peer ledger slot. */
struct ContributionSlotView {
    std::optional<ContributionKind> pending{};
    std::optional<ContributionKind> inFlight{};
    std::optional<ContributionKind> lastCommitted{};
    std::uint64_t desiredRevision{};
    std::uint64_t committedRevision{};
    bool desiredKnown{};
    bool desiredPresent{};
    bool committedPresent{};
};

/** Tracks pending, in-flight, and committed entity state for one peer. */
class PeerContributionLedger {
public:
    /** Replaces the peer view with create baselines for every live entity. */
    void reset_for_late_join(const ReplicationWorldState& world) noexcept;

    /** Publishes a newly allocated entity as the peer's desired state. */
    [[nodiscard]] bool publish_create(const EntitySnapshot& entity) noexcept;

    /** Publishes a newer live revision as the peer's desired state. */
    [[nodiscard]] bool publish_update(const EntitySnapshot& entity) noexcept;

    /** Publishes a released entity tombstone as the peer's desired state. */
    [[nodiscard]] bool publish_remove(const EntitySnapshot& entity) noexcept;

    /** Moves the next pending action into one packet-owned contribution. */
    [[nodiscard]] std::optional<ReplicationContribution> begin_next(std::uint64_t nowTick) noexcept;

    /** Commits an exact in-flight contribution after packet success. */
    [[nodiscard]] bool commit(const ReplicationContribution& contribution) noexcept;

    /** Requeues an exact in-flight contribution after packet loss. */
    [[nodiscard]] bool requeue_loss(const ReplicationContribution& contribution) noexcept;

    /** Requeues every contribution whose send age reached the timeout. */
    [[nodiscard]] std::size_t requeue_timeouts(std::uint64_t nowTick,
                                               std::uint64_t timeoutTicks) noexcept;

    /** Returns the number of actions not yet attached to a packet. */
    [[nodiscard]] std::size_t pending_count() const noexcept;

    /** Returns the number of actions owned by uncommitted packets. */
    [[nodiscard]] std::size_t in_flight_count() const noexcept;

    /** Returns the number of entities known to exist at the peer. */
    [[nodiscard]] std::size_t committed_count() const noexcept;

    /** Returns one ledger slot without exposing mutable state. */
    [[nodiscard]] ContributionSlotView view(std::uint16_t index) const noexcept;

private:
    /** One desired or committed presence state. */
    struct PresenceState {
        EntitySnapshot entity{};
        bool known{};
        bool present{};
    };

    /** One contribution waiting for a packet outcome. */
    struct InFlightState {
        ReplicationContribution contribution{};
        std::uint64_t sentTick{};
        bool occupied{};
    };

    /** All peer state for one entity slot. */
    struct SlotState {
        PresenceState desired{};
        PresenceState committed{};
        InFlightState inFlight{};
        ContributionKind lastCommitted{ContributionKind::create};
        bool hasLastCommitted{};
    };

    /** Publishes one monotonic desired state. */
    [[nodiscard]] bool publish(const EntitySnapshot& entity, bool present) noexcept;

    /** Returns the action still needed after the in-flight action succeeds. */
    [[nodiscard]] static std::optional<ContributionKind>
    pending_kind(const SlotState& slot) noexcept;

    /** Returns the state produced by the current in-flight action. */
    [[nodiscard]] static PresenceState projected_state(const SlotState& slot) noexcept;

    /** Builds an action from committed state to desired state. */
    [[nodiscard]] ReplicationContribution make_contribution(std::uint16_t index,
                                                            ContributionKind kind) noexcept;

    /** Confirms that a packet outcome belongs to the current in-flight action. */
    [[nodiscard]] bool matches(const ReplicationContribution& contribution) const noexcept;

    /** Advances the nonzero token sequence. */
    [[nodiscard]] std::uint64_t advance_transmission() noexcept;

    /** Advances the nonzero peer-lifecycle epoch. */
    void advance_epoch() noexcept;

    std::array<SlotState, kReplicationEntityCapacity> slots_{};
    std::size_t scanCursor_{};
    std::uint64_t epoch_{kFirstLedgerEpoch};
    std::uint64_t transmission_{};
};

} // namespace sunrise::state::gameplay::external
