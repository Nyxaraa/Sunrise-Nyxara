#pragma once

#include <cstdint>

#include "../definition.h"

namespace sunrise::state::activity::bubble_authority {

/**
 * Renews released authority only when the client enters that bubble again.
 * @param sessionId Joined activity session.
 * @param sliceSetIndex Slice set the client is in, or the destination's own.
 * @param grant Gets the bubble and its token.
 * @param enteringBubble Whether this snapshot enters a different held bubble.
 * @return True when a bubble is owed.
 */
[[nodiscard]] bool select_grant(std::uint64_t sessionId,
                                std::int32_t sliceSetIndex,
                                Grant& grant,
                                bool enteringBubble = false) noexcept;

/**
 * Records a bubble as granted so it is not granted twice.
 * @param sessionId Joined activity session.
 * @param grant Bubble and token that went out.
 */
void record_grant(std::uint64_t sessionId, const Grant& grant) noexcept;

/**
 * Releases authority while preserving the token the client last received.
 * @param sessionId Joined activity session.
 * @param bubble Bubble named by the accepted abdication.
 * @param mask Optional exact released entities, merged with earlier reports.
 */
void record_abdication(std::uint64_t sessionId,
                       std::uint8_t bubble,
                       const EntitySlotMask* mask = nullptr) noexcept;

/**
 * Copies the released entities awaiting a claim in one bubble.
 * @param sessionId Joined activity session.
 * @param bubble Bubble whose released entities are requested.
 * @param output Receives the exact pending mask, cleared on failure.
 * @return True when at least one released entity is pending.
 */
[[nodiscard]] bool snapshot_released_entities(std::uint64_t sessionId,
                                              std::uint8_t bubble,
                                              EntitySlotMask& output) noexcept;

/**
 * Removes only the released entities covered by a delivered claim.
 * @param sessionId Joined activity session.
 * @param bubble Bubble receiving the claim.
 * @param mask Exact mask delivered to the client.
 */
void record_claim(std::uint64_t sessionId,
                  std::uint8_t bubble,
                  const EntitySlotMask& mask) noexcept;

/**
 * Removes delivered purge bits from every bubble in one session.
 * @param sessionId Joined activity session.
 * @param mask Exact entity mask delivered by the purge.
 */
void record_purge(std::uint64_t sessionId, const EntitySlotMask& mask) noexcept;

/**
 * Drops every grant recorded for one session, so the next roster push grants again.
 * A join resets the client's roster container. Keeping the old grant set would leave the new
 * container ungranted.
 * @param sessionId Joined activity session.
 */
void clear_grants(std::uint64_t sessionId) noexcept;

} // namespace sunrise::state::activity::bubble_authority
