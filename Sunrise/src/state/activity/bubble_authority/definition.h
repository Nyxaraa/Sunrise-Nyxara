#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../../middleware/bap/activity_message/entity_slots.h"

namespace sunrise::state::activity::bubble_authority {

using EntitySlotMask = middleware::bap::activity_message::entity_slots::EntitySlotMask;

/** 64 usable bubbles and one first-send fallback own grant tokens. */
inline constexpr std::size_t kAuthoritySlotCount = 65;
/** Bubble 64 is sent with the first usable bubble and never derived from a slice set. */
inline constexpr std::uint8_t kFallbackBubble = 64;
/** Slice-set indices 0 to 511 map into the 64 usable bubbles. */
inline constexpr std::int32_t kMaximumGrantSliceSetIndex = 511;
/** Dividing a slice-set index by 8 gives its bubble index. */
inline constexpr std::uint8_t kSliceSetToBubbleShift = 3;
/** The client's cleared mirror changes when the first nonzero token arrives. */
inline constexpr std::uint16_t kInitialGrantToken = 1;
/** The token rides a 16-bit field, so it saturates here rather than wrapping onto a live value. */
inline constexpr std::uint16_t kMaximumGrantToken = 0xFFFF;
/** The cleared grant slot uses a value outside the 65-entry authority table. */
inline constexpr std::uint8_t kInvalidBubble = 0xFF;

/** One changed per-bubble token picked under the State lock. */
struct Grant final {
    std::uint8_t bubble{kInvalidBubble};
    std::uint16_t token{};
};

/** Persistent grant-token mirrors owned by one activity session. */
struct AuthorityState final {
    /** Token in force per bubble. Zero means the bubble is owed a grant. */
    std::array<std::uint16_t, kAuthoritySlotCount> grantTokens{};
    /**
     * Highest token ever issued per bubble, which a release does not clear.
     * The client ignores a token its mirror already holds, so a re-grant must exceed this.
     */
    std::array<std::uint16_t, kAuthoritySlotCount> issuedTokens{};
    /** True while the client holds the bubble. An abdication clears it. */
    std::array<bool, kAuthoritySlotCount> held{};
    /** Released entities remain pending until their exact claim is delivered. */
    std::array<EntitySlotMask, kAuthoritySlotCount> releasedEntities{};
};

} // namespace sunrise::state::activity::bubble_authority
