#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "sensor_message.h"

namespace sunrise::middleware::bap::activity_message::sensor_message {
namespace {

namespace bits = encoding::bits;

/** Field widths of the message-7 header, in bits. */
constexpr std::uint8_t kRosterKeyBits = 32;
constexpr std::uint8_t kSlotTypeBits = 7;
constexpr std::uint8_t kSlotIndexBits = 16;
constexpr std::uint8_t kSchemaPresenceBits = 1;
constexpr std::uint8_t kSchemaBits = 32;
/** The slot type and index are sent unsigned, biased so their absent value is zero. */
constexpr std::int32_t kSlotTypeBias = 1;
constexpr std::int32_t kSlotIndexBias = 32'768;
/** Slot-type range the package authors; the biased field cannot carry more. */
constexpr std::int8_t kMinimumAuthoredSlotType = 0;
constexpr std::int8_t kMaximumAuthoredSlotType = 126;

/** Checks one exact MSB-first body view, including its unused low padding bits. */
[[nodiscard]] bool packed_body_is_valid(std::span<const std::byte> body,
                                        std::size_t bitCount) noexcept {
    if (bitCount > kMaximumSelectedBodyBits || body.size() != (bitCount + 7U) / 8U) {
        return false;
    }
    const std::size_t usedBits = bitCount % 8U;
    if (usedBits == 0 || body.empty()) {
        return true;
    }
    const std::uint8_t paddingMask = static_cast<std::uint8_t>((1U << (8U - usedBits)) - 1U);
    return (std::to_integer<std::uint8_t>(body.back()) & paddingMask) == 0;
}

/** Writes an exact packed body without interpreting its runtime-selected schema. */
[[nodiscard]] bool write_packed_body(bits::Writer& writer,
                                     std::span<const std::byte> body,
                                     std::size_t bitCount) noexcept {
    const std::size_t wholeBytes = bitCount / 8U;
    for (std::size_t index = 0; index < wholeBytes; ++index) {
        if (!writer.write(std::to_integer<std::uint8_t>(body[index]), 8)) {
            return false;
        }
    }
    const std::uint8_t remainingBits = static_cast<std::uint8_t>(bitCount % 8U);
    if (remainingBits == 0) {
        return true;
    }
    const std::uint8_t finalValue = static_cast<std::uint8_t>(
        std::to_integer<std::uint8_t>(body[wholeBytes]) >> (8U - remainingBits));
    return writer.write(finalValue, remainingBits);
}

/** Checks only gates known before runtime schema selection and component lookup. */
[[nodiscard]] bool authoring_prefix_is_valid(const Body& body) noexcept {
    return body.schemaPresent && body.schema != kAbsentSchema
           && body.target.slotType >= kMinimumAuthoredSlotType
           && body.target.slotType <= kMaximumAuthoredSlotType && body.target.slotIndex != -1;
}

} // namespace

/** Computes the meaningful and padded extents for a schema-present body. */
bool encoded_extent(std::size_t selectedBodyBits,
                    std::size_t& meaningfulBits,
                    std::size_t& bytes) noexcept {
    meaningfulBits = 0;
    bytes = 0;
    if (selectedBodyBits > kMaximumSelectedBodyBits) {
        return false;
    }
    meaningfulBits = kPresentHeaderBits + selectedBodyBits;
    bytes = (meaningfulBits + 7U) / 8U;
    return bytes <= kMaximumPayloadSize;
}

/** Encodes a schema-present envelope. The 56-bit schema-absent form is decode-only. */
bool encode(const Body& body,
            std::span<std::byte> output,
            std::size_t& written,
            std::size_t& writtenBits) noexcept {
    written = 0;
    writtenBits = 0;
    std::size_t meaningfulBits = 0;
    std::size_t byteCount = 0;
    if (!authoring_prefix_is_valid(body)
        || !encoded_extent(body.selectedBodyBits, meaningfulBits, byteCount)
        || !packed_body_is_valid(body.selectedBody, body.selectedBodyBits)
        || output.size() < byteCount) {
        return false;
    }

    bits::Writer writer(output.first(byteCount));
    const std::uint32_t slotType =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(body.target.slotType) + kSlotTypeBias);
    const std::uint32_t slotIndex = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(body.target.slotIndex) + kSlotIndexBias);
    if (!writer.write(body.target.rosterKey, kRosterKeyBits)
        || !writer.write(slotType, kSlotTypeBits) || !writer.write(slotIndex, kSlotIndexBits)
        || !writer.write(1U, kSchemaPresenceBits) || !writer.write(body.schema, kSchemaBits)
        || !write_packed_body(writer, body.selectedBody, body.selectedBodyBits)
        || writer.bit_count() != meaningfulBits || !writer.finish(written)
        || written != byteCount) {
        written = 0;
        return false;
    }
    writtenBits = meaningfulBits;
    return true;
}

/** Decodes one exact envelope using the selected schema's known body bit extent. */
bool decode(std::span<const std::byte> input, std::size_t selectedBodyBits, Body& body) noexcept {
    if (input.size() > kMaximumPayloadSize) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t rosterKey = 0;
    std::uint64_t slotType = 0;
    std::uint64_t slotIndex = 0;
    std::uint64_t schemaPresent = 0;
    if (!reader.read(kRosterKeyBits, rosterKey) || !reader.read(kSlotTypeBits, slotType)
        || !reader.read(kSlotIndexBits, slotIndex)
        || !reader.read(kSchemaPresenceBits, schemaPresent)) {
        return false;
    }

    Body parsed{};
    parsed.target.rosterKey = static_cast<std::uint32_t>(rosterKey);
    parsed.target.slotType =
        static_cast<std::int8_t>(static_cast<std::int32_t>(slotType) - kSlotTypeBias);
    parsed.target.slotIndex =
        static_cast<std::int16_t>(static_cast<std::int32_t>(slotIndex) - kSlotIndexBias);
    parsed.schemaPresent = schemaPresent != 0;
    if (!parsed.schemaPresent) {
        if (selectedBodyBits != 0 || input.size() != kAbsentByteCount
            || reader.remaining_bits() != 0) {
            return false;
        }
        body = parsed;
        return true;
    }

    std::uint64_t schema = 0;
    std::size_t meaningfulBits = 0;
    std::size_t byteCount = 0;
    if (!reader.read(kSchemaBits, schema) || schema == kAbsentSchema
        || !encoded_extent(selectedBodyBits, meaningfulBits, byteCount) || input.size() != byteCount
        || reader.remaining_bits() != input.size() * 8U - kPresentHeaderBits) {
        return false;
    }
    parsed.schema = static_cast<std::uint32_t>(schema);
    parsed.selectedBody = input.subspan(kPresentHeaderBytes);
    parsed.selectedBodyBits = selectedBodyBits;
    if (!packed_body_is_valid(parsed.selectedBody, selectedBodyBits)) {
        return false;
    }
    body = parsed;
    return true;
}

/** Checks one exact envelope without retaining its borrowed selected-body view. */
bool validate(std::span<const std::byte> input, std::size_t selectedBodyBits) noexcept {
    Body body{};
    return decode(input, selectedBodyBits, body);
}

} // namespace sunrise::middleware::bap::activity_message::sensor_message
