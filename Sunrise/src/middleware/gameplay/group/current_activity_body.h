#pragma once

#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::gameplay::group::current_activity {

/** Bits one non-empty body occupies. Nothing after the last field is written. */
inline constexpr std::size_t kBodyBits = 117;
/** First bit of the biased launch reason, counted from the body's root bit. */
inline constexpr std::size_t kReasonBitOffset = 1;
/** First bit of the biased actual-activity index, counted from the body's root bit. */
inline constexpr std::size_t kActualActivityIndexBitOffset = 5;
/** First bit of the biased active-activity index, counted from the body's root bit. */
inline constexpr std::size_t kActivityIndexBitOffset = 17;
/** First bit of the 64-bit nonce, counted from the body's own root bit. */
inline constexpr std::size_t kNonceBitOffset = 32;
/** Logical sentinel encoded as zero in the bias-1 activity-index field. */
inline constexpr std::int16_t kAbsentActivityIndex = -1;
/** Largest logical activity index the 12-bit bias-1 field can carry. */
inline constexpr std::int16_t kMaximumActivityIndex = 4'094;
/** Logical sentinel encoded as zero in the bias-1 launch-reason field. */
inline constexpr std::int8_t kAbsentReason = -1;
/** Largest logical launch reason the 4-bit bias-1 field can carry. */
inline constexpr std::int8_t kMaximumReason = 14;
/** Zero and -1 are the two values the client reads as "this host runs no activity". */
inline constexpr std::uint64_t kAbsentNonce = 0;

/**
 * True when a nonce is one the client will accept as a running activity.
 * @param nonce Candidate selection nonce.
 */
[[nodiscard]] constexpr bool nonce_is_valid(std::uint64_t nonce) noexcept {
    return nonce != kAbsentNonce && nonce != ~std::uint64_t{0};
}

/**
 * True when a descriptor names one concrete activity and one running selection.
 * @param activityIndex Logical active-activity index after removing the wire bias.
 * @param nonce Selection nonce compared with the activity-host parameter.
 */
[[nodiscard]] constexpr bool identity_is_valid(std::int16_t activityIndex,
                                               std::uint64_t nonce) noexcept {
    return activityIndex >= 0 && activityIndex <= kMaximumActivityIndex && nonce_is_valid(nonce);
}

/**
 * True when every scalar needed by the native transition classifier has a wire encoding.
 * The actual-activity index is the descriptor's unconditional field at +2; it is not parameter
 * 8's previous activity.
 */
[[nodiscard]] constexpr bool descriptor_is_valid(std::int8_t reason,
                                                 std::int16_t actualActivityIndex,
                                                 std::int16_t activityIndex,
                                                 std::uint64_t nonce) noexcept {
    return reason >= kAbsentReason && reason <= kMaximumReason
           && actualActivityIndex >= kAbsentActivityIndex
           && actualActivityIndex <= kMaximumActivityIndex
           && identity_is_valid(activityIndex, nonce);
}

/**
 * Writes one `current-activity` body carrying the transition classifier's three leading scalars
 * and the selection nonce. Every other field is written at its absent encoding.
 * @param writer Writer positioned where the parameter body starts.
 * @param reason Logical launch reason after removing the wire bias.
 * @param actualActivityIndex Logical actual-activity index after removing the wire bias.
 * @param activityIndex Logical active-activity index after removing the wire bias.
 * @param nonce Selection nonce, neither zero nor all ones.
 * @return True when all 117 bits fit.
 */
[[nodiscard]] bool write_body(encoding::bits::Writer& writer,
                              std::int8_t reason,
                              std::int16_t actualActivityIndex,
                              std::int16_t activityIndex,
                              std::uint64_t nonce) noexcept;

} // namespace sunrise::middleware::gameplay::group::current_activity
