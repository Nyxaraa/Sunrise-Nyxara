#pragma once

#include <cstddef>
#include <span>

#include "bit_reader.h"
#include "bit_writer.h"

namespace sunrise::middleware::encoding::bits {

/**
 * Writes bytes in memory order.
 * Raw fields copy bytes and keep engine order. Value fields are most-significant-bit first.
 * @param writer Open writer.
 * @param bytes Source bytes.
 * @return True when every byte fit the output.
 */
[[nodiscard]] bool write_raw(Writer& writer, std::span<const std::byte> bytes) noexcept;

/**
 * Reads bytes in memory order.
 * @param reader Open reader.
 * @param bytes Destination, filled completely or not at all.
 * @return True when the whole field was available.
 */
[[nodiscard]] bool read_raw(Reader& reader, std::span<std::byte> bytes) noexcept;

/**
 * Skips bytes in memory order.
 * @param reader Open reader.
 * @param count Byte count to consume.
 * @return True when the whole field was available.
 */
[[nodiscard]] bool skip_raw(Reader& reader, std::size_t count) noexcept;

/**
 * Copies bits straight from a reader into a writer.
 * The stream stays bit aligned, so a body can be moved without a byte boundary.
 * @param source Open reader positioned at the first bit.
 * @param output Open writer.
 * @param bitCount Bits to move.
 * @return True when every bit was available and fit.
 */
[[nodiscard]] bool copy(Reader& source, Writer& output, std::size_t bitCount) noexcept;

/**
 * Reads one raw 64-bit integer.
 * A raw field keeps memory order, so an engine integer arrives low byte first.
 * @param reader Open reader.
 * @param value Receives the host-order value.
 * @return True when all eight bytes were available.
 */
[[nodiscard]] bool read_raw_u64(Reader& reader, std::uint64_t& value) noexcept;

/**
 * Writes one raw 64-bit integer, low byte first.
 * @param writer Open writer.
 * @param value Host-order value.
 * @return True when all eight bytes fit.
 */
[[nodiscard]] bool write_raw_u64(Writer& writer, std::uint64_t value) noexcept;

} // namespace sunrise::middleware::encoding::bits
