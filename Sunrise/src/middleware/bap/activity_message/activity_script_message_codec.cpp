#include <bit>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "script_messages.h"

namespace sunrise::middleware::bap::activity_message::script_messages {
namespace {

namespace bits = encoding::bits;

/** Field widths of the message-40 record and the message-41 event, in bits. */
constexpr std::uint8_t kHashBits = 32;
constexpr std::uint8_t kOffsetBits = 32;
constexpr std::uint8_t kBlobLengthBits = 32;
constexpr std::uint8_t kKindBits = 2;
constexpr std::uint8_t kEventLengthBits = 9;
constexpr std::uint8_t kByteBits = 8;
/** The signed record offset is sent unsigned, biased by the signed 32-bit sign. */
constexpr std::uint32_t kSigned32Sign = 0x80000000U;
/** Value range the two-bit biased kind field can carry. */
constexpr std::int8_t kMinimumKind = -1;
constexpr std::int8_t kMaximumKind = 2;

/** Consumes only zero padding in the final partial byte. */
[[nodiscard]] bool finish_padding(bits::Reader& reader) noexcept {
    const std::size_t paddingBits = reader.remaining_bits();
    std::uint64_t padding = 0;
    return paddingBits < kByteBits && reader.read(static_cast<std::uint8_t>(paddingBits), padding)
           && padding == 0 && reader.remaining_bits() == 0;
}

/** Writes one reflected 128-bit message-40 row. */
[[nodiscard]] bool write_state_record(bits::Writer& writer, const StateRecord& record) noexcept {
    const std::uint32_t offset = std::bit_cast<std::uint32_t>(record.stringOffset) ^ kSigned32Sign;
    return writer.write(record.hashA, kHashBits) && writer.write(record.hashB, kHashBits)
           && writer.write(record.hashC, kHashBits) && writer.write(offset, kOffsetBits);
}

/** Reads one reflected 128-bit message-40 row. */
[[nodiscard]] bool read_state_record(bits::Reader& reader, StateRecord& record) noexcept {
    std::uint64_t hashA = 0;
    std::uint64_t hashB = 0;
    std::uint64_t hashC = 0;
    std::uint64_t offset = 0;
    if (!reader.read(kHashBits, hashA) || !reader.read(kHashBits, hashB)
        || !reader.read(kHashBits, hashC) || !reader.read(kOffsetBits, offset)) {
        return false;
    }
    record.hashA = static_cast<std::uint32_t>(hashA);
    record.hashB = static_cast<std::uint32_t>(hashB);
    record.hashC = static_cast<std::uint32_t>(hashC);
    record.stringOffset =
        std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(offset) ^ kSigned32Sign);
    return true;
}

} // namespace

/** Computes the meaningful bit count for one bounded script-state body. */
bool state_bit_count(std::size_t records, std::size_t blobBytes, std::size_t& bits) noexcept {
    bits = 0;
    if (records > kStateMaximumRecords || blobBytes > kStateMaximumBlobBytes) {
        return false;
    }
    bits =
        kStateCountBits + records * kStateRecordBits + kStateBlobLengthBits + blobBytes * kByteBits;
    return true;
}

/** Computes the padded byte extent for one bounded script-state body. */
bool state_byte_count(std::size_t records, std::size_t blobBytes, std::size_t& bytes) noexcept {
    bytes = 0;
    std::size_t bits = 0;
    if (!state_bit_count(records, blobBytes, bits)) {
        return false;
    }
    bytes = (bits + 7U) / 8U;
    return true;
}

/** Encodes one exact message-40 reflected table and raw blob. */
bool encode_state(const StateBody& body,
                  std::span<std::byte> output,
                  std::size_t& written) noexcept {
    written = 0;
    std::size_t bitCount = 0;
    std::size_t byteCount = 0;
    if (!state_bit_count(body.recordCount, body.blobLength, bitCount)
        || !state_byte_count(body.recordCount, body.blobLength, byteCount)
        || output.size() < byteCount) {
        return false;
    }
    bits::Writer writer(output.first(byteCount));
    if (!writer.write(body.recordCount, static_cast<std::uint8_t>(kStateCountBits))) {
        return false;
    }
    for (std::size_t index = 0; index < body.recordCount; ++index) {
        if (!write_state_record(writer, body.records[index])) {
            return false;
        }
    }
    if (!writer.write(body.blobLength, kBlobLengthBits)) {
        return false;
    }
    for (std::size_t index = 0; index < body.blobLength; ++index) {
        if (!writer.write(std::to_integer<std::uint8_t>(body.blob[index]), kByteBits)) {
            return false;
        }
    }
    return writer.bit_count() == bitCount && writer.finish(written) && written == byteCount;
}

/** Decodes one exact message-40 body with server-side count clamps. */
bool decode_state(std::span<const std::byte> input, StateBody& body) noexcept {
    if (input.empty()) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t count = 0;
    if (!reader.read(static_cast<std::uint8_t>(kStateCountBits), count)
        || count > kStateMaximumRecords) {
        return false;
    }
    StateBody parsed{};
    parsed.recordCount = static_cast<std::uint16_t>(count);
    for (std::size_t index = 0; index < parsed.recordCount; ++index) {
        if (!read_state_record(reader, parsed.records[index])) {
            return false;
        }
    }
    std::uint64_t blobLength = 0;
    if (!reader.read(kBlobLengthBits, blobLength) || blobLength > kStateMaximumBlobBytes) {
        return false;
    }
    parsed.blobLength = static_cast<std::uint32_t>(blobLength);
    std::size_t expectedBytes = 0;
    if (!state_byte_count(parsed.recordCount, parsed.blobLength, expectedBytes)
        || input.size() != expectedBytes) {
        return false;
    }
    for (std::size_t index = 0; index < parsed.blobLength; ++index) {
        std::uint64_t value = 0;
        if (!reader.read(kByteBits, value)) {
            return false;
        }
        parsed.blob[index] = static_cast<std::byte>(value);
    }
    if (!finish_padding(reader)) {
        return false;
    }
    body = parsed;
    return true;
}

/** Checks one exact message-40 body without retaining decoded state. */
bool validate_state(std::span<const std::byte> input) noexcept {
    StateBody body{};
    return decode_state(input, body);
}

/** Computes the meaningful bit count for one bounded script-event body. */
bool event_bit_count(std::size_t payloadBytes, std::size_t& bits) noexcept {
    bits = 0;
    if (payloadBytes > kEventMaximumPayloadBytes) {
        return false;
    }
    bits = kEventHeaderBits + payloadBytes * kByteBits;
    return true;
}

/** Computes the padded byte extent for one bounded script-event body. */
bool event_byte_count(std::size_t payloadBytes, std::size_t& bytes) noexcept {
    bytes = 0;
    std::size_t bits = 0;
    if (!event_bit_count(payloadBytes, bits)) {
        return false;
    }
    bytes = (bits + 7U) / 8U;
    return true;
}

/** Encodes one exact message-41 event record. */
bool encode_event(const EventBody& body,
                  std::span<std::byte> output,
                  std::size_t& written) noexcept {
    written = 0;
    std::size_t bitCount = 0;
    std::size_t byteCount = 0;
    if (body.kind < kMinimumKind || body.kind > kMaximumKind
        || !event_bit_count(body.payloadLength, bitCount)
        || !event_byte_count(body.payloadLength, byteCount) || output.size() < byteCount) {
        return false;
    }
    bits::Writer writer(output.first(byteCount));
    if (!writer.write(body.hashA, kHashBits) || !writer.write(body.hashB, kHashBits)
        || !writer.write(body.hashC, kHashBits)
        || !writer.write(static_cast<std::uint32_t>(body.kind + 1), kKindBits)
        || !writer.write(body.payloadLength, kEventLengthBits)) {
        return false;
    }
    for (std::size_t index = 0; index < body.payloadLength; ++index) {
        const std::uint8_t value =
            static_cast<std::uint8_t>(static_cast<std::int32_t>(body.payload[index]) + 128);
        if (!writer.write(value, kByteBits)) {
            return false;
        }
    }
    return writer.bit_count() == bitCount && writer.finish(written) && written == byteCount;
}

/** Decodes one exact message-41 body with the declared array clamp. */
bool decode_event(std::span<const std::byte> input, EventBody& body) noexcept {
    if (input.empty()) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t hashA = 0;
    std::uint64_t hashB = 0;
    std::uint64_t hashC = 0;
    std::uint64_t kind = 0;
    std::uint64_t payloadLength = 0;
    if (!reader.read(kHashBits, hashA) || !reader.read(kHashBits, hashB)
        || !reader.read(kHashBits, hashC) || !reader.read(kKindBits, kind)
        || !reader.read(kEventLengthBits, payloadLength)
        || payloadLength > kEventMaximumPayloadBytes) {
        return false;
    }
    EventBody parsed{};
    parsed.hashA = static_cast<std::uint32_t>(hashA);
    parsed.hashB = static_cast<std::uint32_t>(hashB);
    parsed.hashC = static_cast<std::uint32_t>(hashC);
    parsed.kind = static_cast<std::int8_t>(static_cast<std::int32_t>(kind) - 1);
    parsed.payloadLength = static_cast<std::uint16_t>(payloadLength);
    std::size_t expectedBytes = 0;
    if (!event_byte_count(parsed.payloadLength, expectedBytes) || input.size() != expectedBytes) {
        return false;
    }
    for (std::size_t index = 0; index < parsed.payloadLength; ++index) {
        std::uint64_t value = 0;
        if (!reader.read(kByteBits, value)) {
            return false;
        }
        parsed.payload[index] = static_cast<std::int8_t>(static_cast<std::int32_t>(value) - 128);
    }
    if (!finish_padding(reader)) {
        return false;
    }
    body = parsed;
    return true;
}

/** Checks one exact message-41 body without retaining decoded state. */
bool validate_event(std::span<const std::byte> input) noexcept {
    EventBody body{};
    return decode_event(input, body);
}

} // namespace sunrise::middleware::bap::activity_message::script_messages
