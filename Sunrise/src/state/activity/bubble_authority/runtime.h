#pragma once

#include <cstdint>

#include "../definition.h"

namespace sunrise::state::activity::bubble_authority {

/**
 * Picks the bubble to hand this session, if one is owed.
 * A bubble is granted once. The token is a change against the client's own mirror, so re-sending
 * the same token for a bubble already granted does nothing, rather than being an error.
 * @param sessionId Joined activity session.
 * @param sliceSetIndex Slice set the client is in, or the destination's own.
 * @param grant Gets the bubble and its token.
 * @return True when a bubble is owed.
 */
[[nodiscard]] bool
select_grant(std::uint64_t sessionId, std::int32_t sliceSetIndex, Grant& grant) noexcept;

/**
 * Records a bubble as granted so it is not granted twice.
 * @param sessionId Joined activity session.
 * @param grant Bubble and token that went out.
 */
void record_grant(std::uint64_t sessionId, const Grant& grant) noexcept;

/**
 * Drops every grant recorded for one session, so the next roster push grants again.
 * A join resets the client's roster container. Keeping the old grant set would leave the new
 * container ungranted.
 * @param sessionId Joined activity session.
 */
void clear_grants(std::uint64_t sessionId) noexcept;

/**
 * Drops one bubble's recorded grant, so re-entering it is granted again.
 * The client abdicates a bubble on leaving it and re-enters the same bubble on any wipe, retry or
 * backtrack. While the record survives that hand-back, `select_grant` sees the bubble as already
 * owed-and-paid and never grants it a second time, so the re-entry runs unauthorised.
 * @param sessionId Joined activity session.
 * @param bubble Bubble the client handed back.
 */
void release_grant(std::uint64_t sessionId, std::uint8_t bubble) noexcept;

} // namespace sunrise::state::activity::bubble_authority
