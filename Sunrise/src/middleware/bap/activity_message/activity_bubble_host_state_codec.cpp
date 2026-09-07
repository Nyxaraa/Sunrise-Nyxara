#include <bit>
#include <limits>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "bubble_host_state.h"

namespace sunrise::middleware::bap::activity_message::bubble_host_state {
namespace {

namespace bits = encoding::bits;

/** Field widths of the reflected message-54 row, in bits. */
constexpr std::uint8_t kStateBits = 3;
constexpr std::uint8_t kSliceSetBits = 10;
constexpr std::uint8_t kPeerBits = 6;
constexpr std::uint8_t kSigned32Bits = 32;
constexpr std::uint8_t kByteBits = 8;
constexpr std::uint8_t kWideBits = 64;
/** Value range each biased field can carry at its width. */
constexpr std::int8_t kMinimumState = -1;
constexpr std::int8_t kMaximumState = 6;
constexpr std::int32_t kMinimumSliceSet = -1;
constexpr std::int32_t kMaximumSliceSet = 1'022;
constexpr std::int8_t kMinimumPeer = -1;
constexpr std::int8_t kMaximumPeer = 62;
/** The host id is sent as an unsigned field biased by the signed 32-bit sign. */
constexpr std::uint32_t kSigned32Bias = 0x80000000U;

/** @return True when every bounded scalar fits its reflected wire field. */
[[nodiscard]] constexpr bool valid(const Row& row) noexcept {
    return row.state >= kMinimumState && row.state <= kMaximumState
           && row.sliceSetIndex >= kMinimumSliceSet && row.sliceSetIndex <= kMaximumSliceSet
           && row.peerIndex >= kMinimumPeer && row.peerIndex <= kMaximumPeer;
}

/** Writes one complete 1,829-bit host row. */
[[nodiscard]] bool write_row(bits::Writer& writer, const Row& row) noexcept {
    const std::uint32_t hostId = std::bit_cast<std::uint32_t>(row.bubbleHostId) + kSigned32Bias;
    if (!writer.write(static_cast<std::uint32_t>(row.state + 1), kStateBits)
        || !writer.write(static_cast<std::uint32_t>(row.sliceSetIndex + 1), kSliceSetBits)
        || !writer.write(static_cast<std::uint32_t>(row.peerIndex + 1), kPeerBits)
        || !writer.write(hostId, kSigned32Bits)) {
        return false;
    }
    for (const std::int8_t value : row.sessionId) {
        if (!writer.write(static_cast<std::uint8_t>(static_cast<std::int32_t>(value) + 128),
                          kByteBits)) {
            return false;
        }
    }
    if (!writer.write(row.unresponsive ? 1U : 0U, 1)) {
        return false;
    }
    for (const std::byte value : row.externalAddress) {
        if (!writer.write(std::to_integer<std::uint8_t>(value), kByteBits)) {
            return false;
        }
    }
    return writer.write(row.unnamed, kWideBits) && writer.write(row.ready ? 1U : 0U, 1);
}

/** Reads one complete row without changing caller state on failure. */
[[nodiscard]] bool read_row(bits::Reader& reader, Row& row) noexcept {
    std::uint64_t state = 0;
    std::uint64_t sliceSet = 0;
    std::uint64_t peer = 0;
    std::uint64_t hostId = 0;
    Row parsed{};
    if (!reader.read(kStateBits, state) || !reader.read(kSliceSetBits, sliceSet)
        || !reader.read(kPeerBits, peer) || !reader.read(kSigned32Bits, hostId)) {
        return false;
    }
    parsed.state = static_cast<std::int8_t>(static_cast<std::int32_t>(state) - 1);
    parsed.sliceSetIndex = static_cast<std::int32_t>(sliceSet) - 1;
    parsed.peerIndex = static_cast<std::int8_t>(static_cast<std::int32_t>(peer) - 1);
    parsed.bubbleHostId =
        std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(hostId) - kSigned32Bias);
    for (std::int8_t& value : parsed.sessionId) {
        std::uint64_t encoded = 0;
        if (!reader.read(kByteBits, encoded)) {
            return false;
        }
        value = static_cast<std::int8_t>(static_cast<std::int32_t>(encoded) - 128);
    }
    std::uint64_t unresponsive = 0;
    if (!reader.read(1, unresponsive)) {
        return false;
    }
    parsed.unresponsive = unresponsive != 0;
    for (std::byte& value : parsed.externalAddress) {
        std::uint64_t encoded = 0;
        if (!reader.read(kByteBits, encoded)) {
            return false;
        }
        value = static_cast<std::byte>(encoded);
    }
    std::uint64_t ready = 0;
    if (!reader.read(kWideBits, parsed.unnamed) || !reader.read(1, ready)) {
        return false;
    }
    parsed.ready = ready != 0;
    if (!valid(parsed)) {
        return false;
    }
    row = parsed;
    return true;
}

/** Consumes only the zero padding left in the final partial byte. */
[[nodiscard]] bool finish_padding(bits::Reader& reader) noexcept {
    const std::size_t paddingBits = reader.remaining_bits();
    std::uint64_t padding = 0;
    return paddingBits < kByteBits && reader.read(static_cast<std::uint8_t>(paddingBits), padding)
           && padding == 0 && reader.remaining_bits() == 0;
}

} // namespace

/** Computes the exact meaningful bit count for a bounded row count. */
bool encoded_bit_count(std::size_t count, std::size_t& bits) noexcept {
    bits = 0;
    if (count > kMaximumRows) {
        return false;
    }
    bits = kCountBits + count * kRowBits;
    return true;
}

/** Computes the exact byte extent, including zero padding in the last byte. */
bool encoded_byte_count(std::size_t count, std::size_t& bytes) noexcept {
    bytes = 0;
    std::size_t bitCount = 0;
    if (!encoded_bit_count(count, bitCount)) {
        return false;
    }
    bytes = (bitCount + 7U) / 8U;
    return true;
}

/** Encodes one complete replacement table in the client's MSB-first reflection order. */
bool encode(const Table& table, std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    std::size_t bitCount = 0;
    std::size_t byteCount = 0;
    if (!encoded_bit_count(table.count, bitCount) || !encoded_byte_count(table.count, byteCount)
        || output.size() < byteCount) {
        return false;
    }
    for (std::size_t index = 0; index < table.count; ++index) {
        if (!valid(table.rows[index])) {
            return false;
        }
    }
    bits::Writer writer(output.first(byteCount));
    if (!writer.write(table.count, static_cast<std::uint8_t>(kCountBits))) {
        return false;
    }
    for (std::size_t index = 0; index < table.count; ++index) {
        if (!write_row(writer, table.rows[index])) {
            return false;
        }
    }
    return writer.bit_count() == bitCount && writer.finish(written) && written == byteCount;
}

/** Decodes one exact message-54 body and rejects nonzero trailing padding. */
bool decode(std::span<const std::byte> input, Table& table) noexcept {
    if (input.empty()) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t count = 0;
    if (!reader.read(static_cast<std::uint8_t>(kCountBits), count) || count > kMaximumRows) {
        return false;
    }
    std::size_t expectedBytes = 0;
    if (!encoded_byte_count(static_cast<std::size_t>(count), expectedBytes)
        || input.size() != expectedBytes) {
        return false;
    }
    Table parsed{};
    parsed.count = static_cast<std::uint8_t>(count);
    for (std::size_t index = 0; index < parsed.count; ++index) {
        if (!read_row(reader, parsed.rows[index])) {
            return false;
        }
    }
    if (!finish_padding(reader)) {
        return false;
    }
    table = parsed;
    return true;
}

/** Checks one exact message-54 body without retaining its decoded state. */
bool validate(std::span<const std::byte> input) noexcept {
    Table table{};
    return decode(input, table);
}

} // namespace sunrise::middleware::bap::activity_message::bubble_host_state
