#include "bit_raw.h"

#include <cstdint>

namespace sunrise::middleware::encoding::bits {

namespace {

/** One raw byte is written as an 8-bit field. */
constexpr std::uint8_t kByteWidth = 8;
/** Mask of one raw byte. */
constexpr std::uint64_t kByteMask = 0xFF;

} // namespace

/** Writes bytes in memory order. */
bool write_raw(Writer& writer, std::span<const std::byte> bytes) noexcept {
    for (const std::byte value : bytes) {
        if (!writer.write(std::to_integer<std::uint64_t>(value), kByteWidth)) {
            return false;
        }
    }
    return true;
}

/** Reads bytes in memory order. */
bool read_raw(Reader& reader, std::span<std::byte> bytes) noexcept {
    for (std::byte& value : bytes) {
        std::uint64_t field = 0;
        if (!reader.read(kByteWidth, field)) {
            return false;
        }
        value = static_cast<std::byte>(field);
    }
    return true;
}

/** Skips bytes in memory order. */
bool skip_raw(Reader& reader, std::size_t count) noexcept {
    return reader.skip(count * kByteWidth);
}

/** Copies bits straight from a reader into a writer. */
bool copy(Reader& source, Writer& output, std::size_t bitCount) noexcept {
    while (bitCount != 0) {
        const auto width = static_cast<std::uint8_t>(bitCount < kByteWidth ? bitCount : kByteWidth);
        std::uint64_t value = 0;
        if (!source.read(width, value) || !output.write(value, width)) {
            return false;
        }
        bitCount -= width;
    }
    return true;
}

/** Reads one raw 64-bit integer. */
bool read_raw_u64(Reader& reader, std::uint64_t& value) noexcept {
    std::uint64_t assembled = 0;
    for (std::size_t index = 0; index < sizeof(std::uint64_t); ++index) {
        std::uint64_t byte = 0;
        if (!reader.read(kByteWidth, byte)) {
            return false;
        }
        assembled |= byte << (index * kByteWidth);
    }
    value = assembled;
    return true;
}

/** Writes one raw 64-bit integer, low byte first. */
bool write_raw_u64(Writer& writer, std::uint64_t value) noexcept {
    for (std::size_t index = 0; index < sizeof(std::uint64_t); ++index) {
        if (!writer.write((value >> (index * kByteWidth)) & kByteMask, kByteWidth)) {
            return false;
        }
    }
    return true;
}

} // namespace sunrise::middleware::encoding::bits
