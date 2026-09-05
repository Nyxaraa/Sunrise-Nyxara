#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::state::activity::bubble_authority {

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
     * The client compares an arriving token against its own mirror and ignores a repeat, so a
     * re-grant after a hand-back has to carry a token it has not already seen. Keeping the issued
     * value separately from the in-force one is what lets the next grant differ.
     */
    std::array<std::uint16_t, kAuthoritySlotCount> issuedTokens{};
};

} // namespace sunrise::state::activity::bubble_authority
