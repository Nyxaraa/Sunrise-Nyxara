#include "activity_membership_query.h"

#include <Windows.h>

#include "../../runtime/storage/internal.h"
#include "../transactions/internal.h"

namespace sunrise::state::activity::membership {

/** Tests whether the client has applied the current membership revision. */
bool acknowledged(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return false;
    }
    bool applied = false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot) {
        const MembershipState& membership = state.sessions[target].membership;
        applied = membership.revision != kAbsentRevision
                  && membership.acknowledgedRevision == membership.revision;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return applied;
}

/** Arms or clears the host-named teleport region for one joined session. */
bool arm_host_teleport(std::uint64_t sessionId,
                       std::int32_t sliceSetIndex,
                       std::uint32_t sliceSetHash) noexcept {
    if (sessionId == kAbsentSessionId) {
        return false;
    }
    bool changed = false;
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot) {
        MembershipState& membership = state.sessions[target].membership;
        if (sliceSetIndex == kAbsentSliceSetIndex) {
            changed = membership.hasHostTeleport;
            membership.hasHostTeleport = false;
            membership.hostTeleport = {};
        } else if (!membership.hasHostTeleport
                   || membership.hostTeleport.sliceSetIndex != sliceSetIndex
                   || membership.hostTeleport.sliceSetHash != sliceSetHash) {
            // Step 0 latches the token, it does not compare it, so the increment is bookkeeping
            // for the client's own arm. The state is what gates the step, and zero is idle.
            const std::uint8_t token =
                static_cast<std::uint8_t>(membership.hostTeleport.token + 1U);
            membership.hostTeleport.sliceSetIndex = sliceSetIndex;
            membership.hostTeleport.sliceSetHash = sliceSetHash;
            membership.hostTeleport.token = token;
            membership.hostTeleport.state = kHostTeleportArmedState;
            membership.hasHostTeleport = true;
            // The client refuses a region record whose per-member token does not equal its own
            // transition count. The initial slice-set load is count 1 and each host teleport adds
            // one, so the published token must advance or the target region never precaches.
            const std::uint8_t current = membership.hasTransitionToken ? membership.transitionToken
                                                                       : kInitialTransitionToken;
            auto advanced = static_cast<std::uint8_t>(current + 1U);
            if (advanced == 0) {
                advanced = kInitialTransitionToken;
            }
            membership.transitionToken = advanced;
            membership.hasTransitionToken = true;
            changed = true;
        }
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return changed;
}

bool arm_hard_wipe(const SessionBinding& binding, std::uint64_t requestKey,
    std::int32_t region, std::uint32_t spawnSetHash) noexcept {
    if (!requestKey || region<0 || !spawnSetHash || spawnSetHash==UINT32_MAX) return false;
    bool accepted=false;
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& state=runtime::storage::g_state.activity;
    const auto target=activity::transactions::find_session(state,binding.sessionId);
    if (target!=kInvalidSessionSlot && state.sessions[target].createdRevision==binding.createdRevision) {
        auto& membership=state.sessions[target].membership;
        auto& wipe=membership.hardWipe;
        if (wipe.requestKey==requestKey) accepted=true;
        else if (!wipe.active && !membership.hasHostTeleport) {
            wipe.requestKey=requestKey;wipe.region=region;wipe.spawnSetHash=spawnSetHash;
            wipe.host={};wipe.host.state=1;
            wipe.host.opaqueByte=static_cast<std::uint8_t>(membership.spawn.opaqueByte+1U);
            wipe.active=true;wipe.resetReady=false;wipe.clientWaiting=false;accepted=true;
        }
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return accepted;
}

bool hard_wipe_needs_publish(std::uint64_t sessionId) noexcept {
    bool pending = false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const auto& state = runtime::storage::g_state.activity;
    const auto target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot) {
        const auto& member = state.sessions[target].membership;
        const auto& wipe = member.hardWipe;
        pending = wipe.active && (wipe.host.state == 4
            || member.spawn.opaqueByte != wipe.host.opaqueByte || member.spawn.state < 1);
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return pending;
}

bool release_hard_wipe(const SessionBinding& binding, std::uint64_t requestKey) noexcept {
    bool accepted = false;
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& state = runtime::storage::g_state.activity;
    const auto target = activity::transactions::find_session(state, binding.sessionId);
    if (target != kInvalidSessionSlot && state.sessions[target].createdRevision == binding.createdRevision) {
        auto& wipe = state.sessions[target].membership.hardWipe;
        if (wipe.active && wipe.requestKey == requestKey) { wipe.release(); accepted = true; }
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return accepted;
}

std::uint32_t checkpoint_spawn_hash(std::uint64_t sessionId, std::int32_t region) noexcept {
    std::uint32_t result{};
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state=runtime::storage::g_state.activity;
    const auto target=activity::transactions::find_session(state,sessionId);
    if (target!=kInvalidSessionSlot) {
        const auto& wipe=state.sessions[target].membership.hardWipe;
        if (wipe.requestKey && wipe.region==region) result=wipe.spawnSetHash;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return result;
}

/** Reports whether a host-named teleport is still waiting for the client to move. */
bool host_teleport_armed(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return false;
    }
    bool armed = false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot) {
        const MembershipState& membership = state.sessions[target].membership;
        // The spawn state is the arm after the client has moved. It still rides every body, but it
        // is no longer waiting on anything, so it must not keep forcing revisions.
        armed =
            membership.hasHostTeleport && membership.hostTeleport.state != kHostTeleportSpawnState;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return armed;
}

/** Reads the pending region the client last reported. */
std::int32_t reported_region(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return kAbsentRegionIndex;
    }
    std::int32_t region = kAbsentRegionIndex;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot) {
        region = state.sessions[target].membership.region.index;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return region;
}

/** Reads the region the client is in: the current leg, else the pending one. */
std::int32_t player_region(std::uint64_t sessionId) noexcept {
    const ClientPlacement placement = reported_placement(sessionId);
    return placement.currentRegion >= 0 ? placement.currentRegion : placement.region;
}

/** Reads the slice set from the client's newest D6 teleport state. */
std::int32_t reported_slice_set(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return kAbsentSliceSetIndex;
    }
    std::int32_t sliceSet = kAbsentSliceSetIndex;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot) {
        sliceSet = state.sessions[target].membership.teleport.sliceSetIndex;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return sliceSet;
}

/** Reads the client's region legs and current bubble together. */
ClientPlacement reported_placement(std::uint64_t sessionId) noexcept {
    ClientPlacement placement{};
    if (sessionId == kAbsentSessionId) {
        return placement;
    }
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot) {
        const MembershipState& membership = state.sessions[target].membership;
        placement.region = membership.region.index;
        placement.currentRegion = membership.currentRegion.index;
        placement.bubble = membership.bubble;
        placement.bubbleRevision = membership.bubbleRevision;
        placement.entered = membership.entered;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return placement;
}

/** Records the world state the client's character write-back reports, on every joined session. */
void note_client_writeback(bool inWorld) noexcept {
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& state = runtime::storage::g_state.activity;
    for (SessionRecord& record : state.sessions) {
        if (record.occupied && record.joined) {
            record.membership.entered = inWorld && record.membership.currentRegion.index >= 0;
        }
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
}

/** Names the region the client has instantiated. */
std::int32_t instantiated_region(const ClientPlacement& placement) noexcept {
    return placement.currentRegion >= 0 ? placement.currentRegion : kAbsentRegionIndex;
}

/** Reads the newest session the client has reported a region on. */
std::uint64_t live_region_session(std::uint64_t fallback) noexcept {
    std::uint64_t newest = kAbsentSessionId;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    for (const SessionRecord& record : runtime::storage::g_state.activity.sessions) {
        if (record.occupied && record.sessionId > newest
            && (record.membership.region.index > kAbsentRegionIndex
                || record.membership.currentRegion.index > kAbsentRegionIndex)) {
            newest = record.sessionId;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return newest == kAbsentSessionId ? fallback : newest;
}

/** Reads the member key the client joined this activity session under. */
std::uint64_t member_key(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return 0;
    }
    std::uint64_t key = 0;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot && state.sessions[target].membership.hasIdentity) {
        key = state.sessions[target].membership.identity.memberKey;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return key;
}

/** Reads the identity value message 12 publishes at member record `+16`. */
std::uint64_t join_identity(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return 0;
    }
    std::uint64_t identity = 0;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot && state.sessions[target].membership.hasIdentity) {
        identity = state.sessions[target].membership.identity.joinIdentity;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return identity;
}

} // namespace sunrise::state::activity::membership
