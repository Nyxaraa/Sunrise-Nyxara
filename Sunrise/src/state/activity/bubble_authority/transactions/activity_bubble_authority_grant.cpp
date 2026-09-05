#include <Windows.h>

#include "../../../runtime/storage/internal.h"
#include "../../transactions/internal.h"
#include "../runtime.h"

namespace sunrise::state::activity::bubble_authority {

/** Picks the bubble to hand this session, if one is owed. */
bool select_grant(std::uint64_t sessionId, std::int32_t sliceSetIndex, Grant& grant) noexcept {
    grant = {};
    if (sessionId == kAbsentSessionId || sliceSetIndex < 0
        || sliceSetIndex > kMaximumGrantSliceSetIndex) {
        return false;
    }
    const auto bubble = static_cast<std::uint8_t>(sliceSetIndex >> kSliceSetToBubbleShift);
    bool owed = false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot && bubble < kFallbackBubble
        && state.sessions[target].bubbleAuthority.grantTokens[bubble] == 0) {
        // A bubble handed back and re-entered must be granted a token the client's mirror has not
        // already seen, so the next one follows the highest ever issued rather than restarting.
        const std::uint16_t issued = state.sessions[target].bubbleAuthority.issuedTokens[bubble];
        grant.bubble = bubble;
        grant.token = issued < kMaximumGrantToken
                          ? static_cast<std::uint16_t>(issued + 1)
                          : kMaximumGrantToken;
        owed = true;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return owed;
}

/** Records a bubble as granted so it is not granted twice. */
void record_grant(std::uint64_t sessionId, const Grant& grant) noexcept {
    if (sessionId == kAbsentSessionId || grant.bubble >= kAuthoritySlotCount || grant.token == 0) {
        return;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot) {
        state.sessions[target].bubbleAuthority.grantTokens[grant.bubble] = grant.token;
        state.sessions[target].bubbleAuthority.issuedTokens[grant.bubble] = grant.token;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
}

/** Drops one bubble's recorded grant, so re-entering it is granted again. */
void release_grant(std::uint64_t sessionId, std::uint8_t bubble) noexcept {
    if (sessionId == kAbsentSessionId || bubble >= kAuthoritySlotCount) {
        return;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot) {
        // Only the in-force token clears. `issuedTokens` stays so the next grant advances past
        // what the client already mirrors.
        state.sessions[target].bubbleAuthority.grantTokens[bubble] = 0;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
}

/** Drops every grant recorded for one session, so the next roster push grants again. */
void clear_grants(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    ActivityState& state = runtime::storage::g_state.activity;
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot) {
        // Only the in-force tokens clear. `issuedTokens` is what stops a re-grant re-sending a
        // token the client's mirror already holds, and a join that resets the roster container
        // does not reset that mirror, so wiping it here would reintroduce the invisible re-grant.
        state.sessions[target].bubbleAuthority.grantTokens = {};
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
}

} // namespace sunrise::state::activity::bubble_authority
