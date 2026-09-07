#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../state/investment/investment.h"
#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::web_service::messages::family5 {

/**
 * Widest family-5 object body in bytes: 10 presence bits, two 64-bit fields, the 32-bit
 * content-gate mask, and both override lists full at the wire's 127-row limit.
 */
inline constexpr std::size_t kObjectCapacity = 1'135;

/**
 * Validates every bounded family-5 field before encoding.
 * @return True when counts, slots, values, and object identity are safe.
 */
[[nodiscard]] bool valid(const state::Family5State& family) noexcept;

/**
 * Writes all 10 family-5 fields in descriptor order.
 * @param writer Fixed-buffer payload writer.
 * @param family Must already have passed valid().
 * @param serverClockSeconds Server clock the Client extrapolates family-5 time from, in seconds.
 * @return True when the whole nested object fits.
 */
[[nodiscard]] bool write(encoding::bits::Writer& writer,
                         const state::Family5State& family,
                         std::uint64_t serverClockSeconds) noexcept;

/**
 * Encodes the family-5 object on its own, byte-aligned, for a queuez tag-reflection payload.
 * @param family Override lists to publish.
 * @param serverClockSeconds Server clock the Client extrapolates family-5 time from, in seconds.
 * @param output Storage of at least kObjectCapacity bytes; cleared before the first bit.
 * @param written Receives the body size in bytes, trailing pad bits included.
 * @return True when the object is valid and the whole body fits.
 */
[[nodiscard]] bool encode_object(const state::Family5State& family,
                                 std::uint64_t serverClockSeconds,
                                 std::span<std::byte> output,
                                 std::size_t& written) noexcept;

} // namespace sunrise::middleware::web_service::messages::family5
