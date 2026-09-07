#include "state/gameplay/external/replication_world_state.h"

namespace sunrise::state::gameplay::external {
namespace {

/** Returns true when a handle fits the 13-bit and 4-bit fields. */
[[nodiscard]] bool valid_handle(EntityHandle handle) noexcept {
    return handle.slot < kReplicationEntityCapacity && handle.incarnation <= kEntityIncarnationMask;
}

/** Returns true when two snapshots name the same slot allocation. */
[[nodiscard]] bool same_allocation(const EntitySnapshot& left,
                                   const EntitySnapshot& right) noexcept {
    return left.handle == right.handle && left.allocationSequence == right.allocationSequence;
}

/** Returns true when one snapshot can enter a peer ledger. */
[[nodiscard]] bool valid_snapshot(const EntitySnapshot& entity) noexcept {
    return valid_handle(entity.handle) && entity.allocationSequence != 0 && entity.revision != 0;
}

} // namespace

/**
 * Allocates one slot with the next nonzero allocation sequence.
 * @param definition Opaque definition identities retained for encoding.
 * @return A live snapshot, or no value when all slots are live.
 */
std::optional<EntitySnapshot>
ReplicationWorldState::allocate(EntityDefinitionIdentity definition) noexcept {
    std::uint16_t index{};
    if (releasedCount_ != 0) {
        index = releasedSlots_[--releasedCount_];
    } else {
        if (nextPristineSlot_ >= kReplicationEntityCapacity) {
            return std::nullopt;
        }
        index = static_cast<std::uint16_t>(nextPristineSlot_++);
    }

    Slot& slot = slots_[index];
    if (slot.allocationSequence == 0) {
        slot.allocationSequence = kFirstAllocationSequence;
    } else {
        ++slot.allocationSequence;
        if (slot.allocationSequence == 0) {
            slot.allocationSequence = kFirstNonzeroSequence;
        }
    }
    slot.definition = definition;
    slot.revision = advance_revision();
    slot.occupied = true;
    ++liveCount_;
    return EntitySnapshot{
        .handle = {.slot = index, .incarnation = slot.incarnation},
        .definition = slot.definition,
        .revision = slot.revision,
        .allocationSequence = slot.allocationSequence,
    };
}

/**
 * Advances one live entity revision.
 * @param handle Exact live handle to revise.
 * @return The revised snapshot, or no value for a stale handle.
 */
std::optional<EntitySnapshot> ReplicationWorldState::revise(EntityHandle handle) noexcept {
    if (!valid_handle(handle)) {
        return std::nullopt;
    }
    Slot& slot = slots_[handle.slot];
    if (!slot.occupied || slot.incarnation != handle.incarnation) {
        return std::nullopt;
    }
    slot.revision = advance_revision();
    return EntitySnapshot{
        .handle = handle,
        .definition = slot.definition,
        .revision = slot.revision,
        .allocationSequence = slot.allocationSequence,
    };
}

/**
 * Releases one live handle and advances its four-bit incarnation.
 * @param handle Exact live handle to release.
 * @return A removal tombstone, or no value for a stale handle.
 */
std::optional<EntitySnapshot> ReplicationWorldState::release(EntityHandle handle) noexcept {
    if (!valid_handle(handle)) {
        return std::nullopt;
    }
    Slot& slot = slots_[handle.slot];
    if (!slot.occupied || slot.incarnation != handle.incarnation) {
        return std::nullopt;
    }

    const EntitySnapshot removed{
        .handle = handle,
        .definition = slot.definition,
        .revision = advance_revision(),
        .allocationSequence = slot.allocationSequence,
    };
    slot.revision = removed.revision;
    slot.occupied = false;
    slot.incarnation = static_cast<std::uint8_t>((slot.incarnation + 1U) & kEntityIncarnationMask);
    releasedSlots_[releasedCount_++] = handle.slot;
    --liveCount_;
    return removed;
}

/**
 * Reads one live entity without changing its revision.
 * @param handle Exact live handle to read.
 * @return The live snapshot, or no value for a stale handle.
 */
std::optional<EntitySnapshot> ReplicationWorldState::snapshot(EntityHandle handle) const noexcept {
    if (!valid_handle(handle)) {
        return std::nullopt;
    }
    const Slot& slot = slots_[handle.slot];
    if (!slot.occupied || slot.incarnation != handle.incarnation) {
        return std::nullopt;
    }
    return EntitySnapshot{
        .handle = handle,
        .definition = slot.definition,
        .revision = slot.revision,
        .allocationSequence = slot.allocationSequence,
    };
}

/**
 * Reads one live slot for a late-join baseline.
 * @param index Bounded slot index to read.
 * @return The live snapshot, or no value for a free slot.
 */
std::optional<EntitySnapshot>
ReplicationWorldState::snapshot_at(std::uint16_t index) const noexcept {
    if (index >= kReplicationEntityCapacity) {
        return std::nullopt;
    }
    const Slot& slot = slots_[index];
    if (!slot.occupied) {
        return std::nullopt;
    }
    return EntitySnapshot{
        .handle = {.slot = index, .incarnation = slot.incarnation},
        .definition = slot.definition,
        .revision = slot.revision,
        .allocationSequence = slot.allocationSequence,
    };
}

/** @return The number of live entity slots. */
std::size_t ReplicationWorldState::size() const noexcept {
    return liveCount_;
}

/** @return The next nonzero world revision. */
std::uint64_t ReplicationWorldState::advance_revision() noexcept {
    ++revision_;
    if (revision_ == 0) {
        revision_ = kFirstNonzeroSequence;
    }
    return revision_;
}

/**
 * Starts a fresh peer lifecycle from the current live world.
 * @param world World whose live entities become pending creates.
 */
void PeerContributionLedger::reset_for_late_join(const ReplicationWorldState& world) noexcept {
    advance_epoch();
    for (SlotState& slot : slots_) {
        slot = {};
    }
    for (std::size_t index = 0; index < kReplicationEntityCapacity; ++index) {
        const std::optional<EntitySnapshot> entity =
            world.snapshot_at(static_cast<std::uint16_t>(index));
        if (entity.has_value()) {
            slots_[index].desired = PresenceState{
                .entity = *entity,
                .known = true,
                .present = true,
            };
        }
    }
    scanCursor_ = 0;
}

/** @return True when the create became the newest desired revision. */
bool PeerContributionLedger::publish_create(const EntitySnapshot& entity) noexcept {
    return publish(entity, true);
}

/** @return True when the update became the newest desired revision. */
bool PeerContributionLedger::publish_update(const EntitySnapshot& entity) noexcept {
    return publish(entity, true);
}

/** @return True when the removal became the newest desired revision. */
bool PeerContributionLedger::publish_remove(const EntitySnapshot& entity) noexcept {
    return publish(entity, false);
}

/**
 * Assigns the next pending action to a packet token.
 * @param nowTick Monotonic send tick retained for timeout handling.
 * @return The in-flight action, or no value when no slot is ready.
 */
std::optional<ReplicationContribution>
PeerContributionLedger::begin_next(std::uint64_t nowTick) noexcept {
    for (std::size_t offset = 0; offset < kReplicationEntityCapacity; ++offset) {
        const std::size_t index = (scanCursor_ + offset) % kReplicationEntityCapacity;
        SlotState& slot = slots_[index];
        if (slot.inFlight.occupied) {
            continue;
        }
        const std::optional<ContributionKind> kind = pending_kind(slot);
        if (!kind.has_value()) {
            continue;
        }
        ReplicationContribution contribution =
            make_contribution(static_cast<std::uint16_t>(index), *kind);
        slot.inFlight = InFlightState{
            .contribution = contribution,
            .sentTick = nowTick,
            .occupied = true,
        };
        scanCursor_ = (index + 1) % kReplicationEntityCapacity;
        return contribution;
    }
    return std::nullopt;
}

/**
 * Applies one exact packet success to committed peer state.
 * @param contribution Token returned by begin_next.
 * @return True when this token still owns the slot's in-flight action.
 */
bool PeerContributionLedger::commit(const ReplicationContribution& contribution) noexcept {
    if (!matches(contribution)) {
        return false;
    }
    SlotState& slot = slots_[contribution.entity.handle.slot];
    slot.committed.known = true;
    slot.committed.entity = contribution.entity;
    slot.committed.present = contribution.kind != ContributionKind::remove;
    slot.lastCommitted = contribution.kind;
    slot.hasLastCommitted = true;
    slot.inFlight = {};
    return true;
}

/**
 * Returns one exact lost action to the pending comparison.
 * @param contribution Token returned by begin_next.
 * @return True when this token still owns the slot's in-flight action.
 */
bool PeerContributionLedger::requeue_loss(const ReplicationContribution& contribution) noexcept {
    if (!matches(contribution)) {
        return false;
    }
    slots_[contribution.entity.handle.slot].inFlight = {};
    return true;
}

/**
 * Returns every expired action to the pending comparison.
 * @param nowTick Current monotonic tick.
 * @param timeoutTicks Minimum send age to requeue.
 * @return Number of actions requeued.
 */
std::size_t PeerContributionLedger::requeue_timeouts(std::uint64_t nowTick,
                                                     std::uint64_t timeoutTicks) noexcept {
    std::size_t count = 0;
    for (SlotState& slot : slots_) {
        if (!slot.inFlight.occupied || nowTick < slot.inFlight.sentTick
            || nowTick - slot.inFlight.sentTick < timeoutTicks) {
            continue;
        }
        slot.inFlight = {};
        ++count;
    }
    return count;
}

/** @return The number of unsent actions after projected packet success. */
std::size_t PeerContributionLedger::pending_count() const noexcept {
    std::size_t count = 0;
    for (const SlotState& slot : slots_) {
        count += pending_kind(slot).has_value() ? 1U : 0U;
    }
    return count;
}

/** @return The number of actions waiting for packet outcomes. */
std::size_t PeerContributionLedger::in_flight_count() const noexcept {
    std::size_t count = 0;
    for (const SlotState& slot : slots_) {
        count += slot.inFlight.occupied ? 1U : 0U;
    }
    return count;
}

/** @return The number of entities committed as present at the peer. */
std::size_t PeerContributionLedger::committed_count() const noexcept {
    std::size_t count = 0;
    for (const SlotState& slot : slots_) {
        count += slot.committed.present ? 1U : 0U;
    }
    return count;
}

/**
 * Reads one slot without exposing mutable ledger state.
 * @param index Bounded entity slot index.
 * @return The slot view, or an empty view for an invalid index.
 */
ContributionSlotView PeerContributionLedger::view(std::uint16_t index) const noexcept {
    if (index >= kReplicationEntityCapacity) {
        return {};
    }
    const SlotState& slot = slots_[index];
    ContributionSlotView result{
        .pending = pending_kind(slot),
        .desiredRevision = slot.desired.known ? slot.desired.entity.revision : 0,
        .committedRevision = slot.committed.known ? slot.committed.entity.revision : 0,
        .desiredKnown = slot.desired.known,
        .desiredPresent = slot.desired.present,
        .committedPresent = slot.committed.present,
    };
    if (slot.inFlight.occupied) {
        result.inFlight = slot.inFlight.contribution.kind;
    }
    if (slot.hasLastCommitted) {
        result.lastCommitted = slot.lastCommitted;
    }
    return result;
}

/**
 * Replaces one desired state only with a newer valid revision.
 * @param entity Snapshot or tombstone to publish.
 * @param present True for create or update, and false for remove.
 * @return True when the desired state changed.
 */
bool PeerContributionLedger::publish(const EntitySnapshot& entity, bool present) noexcept {
    if (!valid_snapshot(entity)) {
        return false;
    }
    SlotState& slot = slots_[entity.handle.slot];
    if (slot.desired.known && entity.revision <= slot.desired.entity.revision) {
        return false;
    }
    slot.desired = PresenceState{
        .entity = entity,
        .known = true,
        .present = present,
    };
    return true;
}

/**
 * Derives the action needed after the current in-flight action succeeds.
 * @param slot Slot state to compare.
 * @return The pending action, or no value when projected state matches.
 */
std::optional<ContributionKind>
PeerContributionLedger::pending_kind(const SlotState& slot) noexcept {
    if (!slot.desired.known) {
        return std::nullopt;
    }
    const PresenceState base = projected_state(slot);
    if (base.present) {
        if (!slot.desired.present || !same_allocation(base.entity, slot.desired.entity)) {
            return ContributionKind::remove;
        }
        if (slot.desired.entity.revision > base.entity.revision) {
            return ContributionKind::update;
        }
        return std::nullopt;
    }
    if (slot.desired.present) {
        return ContributionKind::create;
    }
    return std::nullopt;
}

/**
 * Applies the in-flight action to a temporary committed state.
 * @param slot Slot state to project.
 * @return State expected after packet success.
 */
PeerContributionLedger::PresenceState
PeerContributionLedger::projected_state(const SlotState& slot) noexcept {
    PresenceState projected = slot.committed;
    if (!slot.inFlight.occupied) {
        return projected;
    }
    projected.known = true;
    projected.entity = slot.inFlight.contribution.entity;
    projected.present = slot.inFlight.contribution.kind != ContributionKind::remove;
    return projected;
}

/**
 * Captures one pending action as an epoch-bound transmission token.
 * @param index Entity slot that owns the action.
 * @param kind Action derived from committed and desired state.
 * @return Contribution ready to become in flight.
 */
ReplicationContribution PeerContributionLedger::make_contribution(std::uint16_t index,
                                                                  ContributionKind kind) noexcept {
    SlotState& slot = slots_[index];
    EntitySnapshot entity = slot.desired.entity;
    if (kind == ContributionKind::remove) {
        entity.handle = slot.committed.entity.handle;
        entity.definition = slot.committed.entity.definition;
        entity.allocationSequence = slot.committed.entity.allocationSequence;
    }
    return ReplicationContribution{
        .entity = entity,
        .ledgerEpoch = epoch_,
        .transmissionId = advance_transmission(),
        .kind = kind,
    };
}

/**
 * Checks one packet outcome against exact in-flight ownership.
 * @param contribution Packet contribution to check.
 * @return True only for the current epoch and transmission.
 */
bool PeerContributionLedger::matches(const ReplicationContribution& contribution) const noexcept {
    if (!valid_snapshot(contribution.entity) || contribution.ledgerEpoch != epoch_) {
        return false;
    }
    const InFlightState& inFlight = slots_[contribution.entity.handle.slot].inFlight;
    return inFlight.occupied && inFlight.contribution == contribution;
}

/** @return The next nonzero transmission identifier. */
std::uint64_t PeerContributionLedger::advance_transmission() noexcept {
    ++transmission_;
    if (transmission_ == 0) {
        transmission_ = kFirstNonzeroSequence;
    }
    return transmission_;
}

/** Starts a new nonzero peer lifecycle epoch. */
void PeerContributionLedger::advance_epoch() noexcept {
    ++epoch_;
    if (epoch_ == 0) {
        epoch_ = kFirstLedgerEpoch;
    }
}

} // namespace sunrise::state::gameplay::external
