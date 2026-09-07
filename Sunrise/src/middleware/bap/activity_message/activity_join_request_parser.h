#pragma once

#include <cstddef>
#include <span>

#include "definition.h"

namespace sunrise::middleware::bap::activity_message::join_request {

/** Correlation, session, and member key occupy the first twenty body bytes. */
inline constexpr std::size_t kScalarPrefixSize = 20;
/** The reflection root has 570 mandatory meaningful bits. */
inline constexpr std::size_t kMinimumEncodedBits = 570;
/** Complete bytes needed to carry the 570-bit minimum. */
inline constexpr std::size_t kMinimumEncodedSize = (kMinimumEncodedBits + 7) / 8;

/**
 * Extracts the correlation, nonzero session, member key, and character from an activity join
 * request that reaches the reflection root's proved minimum. Bits past that minimum are not read,
 * and a nonzero pad bit or a trailing byte is not a reason to refuse the body.
 * @param input Complete join-request activity payload.
 * @param request Cleared first. Receives fields only on success.
 * @return True when the 570-bit minimum is there and its session is nonzero.
 */
[[nodiscard]] bool parse_join_request(std::span<const std::byte> input,
                                      JoinRequest& request) noexcept;

} // namespace sunrise::middleware::bap::activity_message::join_request
