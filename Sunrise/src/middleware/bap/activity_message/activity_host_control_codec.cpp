#include <bit>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "activity_host_control.h"

namespace sunrise::middleware::bap::activity_message::host_control {
namespace {

namespace bits = middleware::encoding::bits;

/** Primitive widths and biases used by this fixed reflection group. */
constexpr std::uint8_t kByteWidth = 8;
constexpr std::uint8_t kReasonWidth = 3;
constexpr std::int32_t kReasonBias = 1;
constexpr std::uint8_t kSigned32Width = 32;
constexpr std::uint32_t kSigned32Bias = 0x80000000U;
constexpr std::uint8_t kBoolWidth = 1;

/** @return True when the reason fits the 3-bit biased range the wire allows. */
[[nodiscard]] constexpr bool valid_reason(std::int8_t reason) noexcept {
    return reason >= kMinimumReason && reason <= kMaximumReason;
}

/** @return True when only the unused tail bits remain and they are zero. */
[[nodiscard]] bool finish_padding(bits::Reader& reader) noexcept {
    const std::size_t paddingBits = reader.remaining_bits();
    std::uint64_t padding = 0;
    return paddingBits < kByteWidth && reader.read(static_cast<std::uint8_t>(paddingBits), padding)
           && padding == 0 && reader.remaining_bits() == 0;
}

/** Applies the signed midpoint bias before a 32-bit field is packed. */
[[nodiscard]] constexpr std::uint32_t encode_signed32(std::int32_t value) noexcept {
    return std::bit_cast<std::uint32_t>(value) + kSigned32Bias;
}

/** Removes the signed midpoint bias after a 32-bit field is unpacked. */
[[nodiscard]] constexpr std::int32_t decode_signed32(std::uint64_t raw) noexcept {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(raw) - kSigned32Bias);
}

/** Encodes the common message-28 and message-30 scalar body. */
[[nodiscard]] bool encode_correlation(const AuthorityMaskRequestBody& body,
                                      std::span<std::byte> output,
                                      std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kCorrelationByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kCorrelationByteCount));
    return writer.write(encode_signed32(body.correlation), kSigned32Width)
           && writer.bit_count() == kCorrelationBitCount && writer.finish(written)
           && written == kCorrelationByteCount;
}

/** Decodes the common message-28 and message-30 scalar body. */
[[nodiscard]] bool decode_correlation(std::span<const std::byte> input,
                                      AuthorityMaskRequestBody& body) noexcept {
    if (input.size() != kCorrelationByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t raw = 0;
    if (!reader.read(kSigned32Width, raw) || !finish_padding(reader)) {
        return false;
    }
    const AuthorityMaskRequestBody parsed{decode_signed32(raw)};
    body = parsed;
    return true;
}

} // namespace

/** Encodes one complete message-24 body. */
bool encode_claim_authority(const ClaimAuthorityBody& body,
                            std::span<std::byte> output,
                            std::size_t& written) noexcept {
    written = 0;
    if (!valid_reason(body.reason) || output.size() < kClaimAuthorityByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kClaimAuthorityByteCount));
    const std::uint32_t storedReason =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(body.reason) + kReasonBias);
    return writer.write(body.epoch, kByteWidth) && entity_slots::write_mask(writer, body.slots)
           && writer.write(storedReason, kReasonWidth)
           && writer.bit_count() == kClaimAuthorityBitCount && writer.finish(written)
           && written == kClaimAuthorityByteCount;
}

/** Decodes one exact message-24 body. */
bool decode_claim_authority(std::span<const std::byte> input, ClaimAuthorityBody& body) noexcept {
    if (input.size() != kClaimAuthorityByteCount) {
        return false;
    }
    bits::Reader reader(input);
    ClaimAuthorityBody parsed{};
    std::uint64_t epoch = 0;
    std::uint64_t storedReason = 0;
    if (!reader.read(kByteWidth, epoch) || !entity_slots::read_mask(reader, parsed.slots)
        || !reader.read(kReasonWidth, storedReason) || !finish_padding(reader)) {
        return false;
    }
    parsed.epoch = static_cast<std::uint8_t>(epoch);
    parsed.reason = static_cast<std::int8_t>(static_cast<std::int32_t>(storedReason) - kReasonBias);
    body = parsed;
    return true;
}

/** Checks one exact message-24 body. */
bool validate_retirement_authority(std::span<const std::byte> input) noexcept {
    ClaimAuthorityBody body{};
    return decode_claim_authority(input, body);
}

/** Encodes one complete message-25 body. */
bool encode_purge_authority(const PurgeAuthorityBody& body,
                            std::span<std::byte> output,
                            std::size_t& written) noexcept {
    written = 0;
    if (!valid_reason(body.reason) || output.size() < kPurgeAuthorityByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kPurgeAuthorityByteCount));
    const std::uint32_t storedReason =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(body.reason) + kReasonBias);
    return writer.write(storedReason, kReasonWidth) && writer.write(body.epoch, kByteWidth)
           && entity_slots::write_mask(writer, body.slots)
           && writer.bit_count() == kPurgeAuthorityBitCount && writer.finish(written)
           && written == kPurgeAuthorityByteCount;
}

/** Decodes one exact message-25 body. */
bool decode_purge_authority(std::span<const std::byte> input, PurgeAuthorityBody& body) noexcept {
    if (input.size() != kPurgeAuthorityByteCount) {
        return false;
    }
    bits::Reader reader(input);
    PurgeAuthorityBody parsed{};
    std::uint64_t epoch = 0;
    std::uint64_t storedReason = 0;
    if (!reader.read(kReasonWidth, storedReason) || !reader.read(kByteWidth, epoch)
        || !entity_slots::read_mask(reader, parsed.slots) || !finish_padding(reader)) {
        return false;
    }
    parsed.epoch = static_cast<std::uint8_t>(epoch);
    parsed.reason = static_cast<std::int8_t>(static_cast<std::int32_t>(storedReason) - kReasonBias);
    body = parsed;
    return true;
}

/** Checks one exact message-25 body. */
bool validate_purge_authority(std::span<const std::byte> input) noexcept {
    PurgeAuthorityBody body{};
    return decode_purge_authority(input, body);
}

/** Encodes one complete message-28 body. */
bool encode_reset_authority_mask(const AuthorityMaskRequestBody& body,
                                 std::span<std::byte> output,
                                 std::size_t& written) noexcept {
    return encode_correlation(body, output, written);
}

/** Decodes one exact message-28 body. */
bool decode_reset_authority_mask(std::span<const std::byte> input,
                                 AuthorityMaskRequestBody& body) noexcept {
    return decode_correlation(input, body);
}

/** Checks one exact message-28 body. */
bool validate_reset_authority_mask(std::span<const std::byte> input) noexcept {
    AuthorityMaskRequestBody body{};
    return decode_reset_authority_mask(input, body);
}

/** Encodes one complete message-30 body. */
bool encode_query_authority_mask(const AuthorityMaskRequestBody& body,
                                 std::span<std::byte> output,
                                 std::size_t& written) noexcept {
    return encode_correlation(body, output, written);
}

/** Decodes one exact message-30 body. */
bool decode_query_authority_mask(std::span<const std::byte> input,
                                 AuthorityMaskRequestBody& body) noexcept {
    return decode_correlation(input, body);
}

/** Checks one exact message-30 body. */
bool validate_query_authority_mask(std::span<const std::byte> input) noexcept {
    AuthorityMaskRequestBody body{};
    return decode_query_authority_mask(input, body);
}

/** Returns the exact message-45 bit count for a safe key count. */
std::size_t reservations_failed_bit_count(std::size_t keyCount) noexcept {
    if (keyCount > kReservationCapacity) {
        return 0;
    }
    return kReservationCountBitCount + keyCount * kReservationKeySize * kByteWidth;
}

/** Returns the exact message-45 byte count for a safe key count. */
std::size_t reservations_failed_byte_count(std::size_t keyCount) noexcept {
    const std::size_t bitCount = reservations_failed_bit_count(keyCount);
    return bitCount == 0 ? 0 : (bitCount + kByteWidth - 1) / kByteWidth;
}

/** Encodes one complete bounded message-45 body. */
bool encode_reservations_failed(const ReservationsFailedBody& body,
                                std::span<std::byte> output,
                                std::size_t& written) noexcept {
    written = 0;
    const std::size_t bitCount = reservations_failed_bit_count(body.keyCount);
    const std::size_t byteCount = reservations_failed_byte_count(body.keyCount);
    if (bitCount == 0 || byteCount == 0 || output.size() < byteCount) {
        return false;
    }
    bits::Writer writer(output.first(byteCount));
    if (!writer.write(body.keyCount, static_cast<std::uint8_t>(kReservationCountBitCount))) {
        return false;
    }
    for (std::size_t keyIndex = 0; keyIndex < body.keyCount; ++keyIndex) {
        for (const std::byte value : body.keys[keyIndex]) {
            if (!writer.write(std::to_integer<std::uint8_t>(value), kByteWidth)) {
                return false;
            }
        }
    }
    return writer.bit_count() == bitCount && writer.finish(written) && written == byteCount;
}

/** Decodes one exact bounded message-45 body. */
bool decode_reservations_failed(std::span<const std::byte> input,
                                ReservationsFailedBody& body) noexcept {
    if (input.empty() || input.size() > kReservationsFailedMaximumByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t keyCount = 0;
    if (!reader.read(static_cast<std::uint8_t>(kReservationCountBitCount), keyCount)
        || keyCount > kReservationCapacity
        || input.size() != reservations_failed_byte_count(static_cast<std::size_t>(keyCount))) {
        return false;
    }
    ReservationsFailedBody parsed{};
    parsed.keyCount = static_cast<std::size_t>(keyCount);
    for (std::size_t keyIndex = 0; keyIndex < parsed.keyCount; ++keyIndex) {
        for (std::byte& value : parsed.keys[keyIndex]) {
            std::uint64_t raw = 0;
            if (!reader.read(kByteWidth, raw)) {
                return false;
            }
            value = static_cast<std::byte>(raw);
        }
    }
    if (!finish_padding(reader)) {
        return false;
    }
    body = parsed;
    return true;
}

/** Checks one exact bounded message-45 body. */
bool validate_reservations_failed(std::span<const std::byte> input) noexcept {
    ReservationsFailedBody body{};
    return decode_reservations_failed(input, body);
}

/** Encodes one complete message-56 body. */
bool encode_perf_request_kill(const PerfRequestKillBody& body,
                              std::span<std::byte> output,
                              std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kPerfRequestKillByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kPerfRequestKillByteCount));
    return writer.write(encode_signed32(body.value), kSigned32Width)
           && writer.bit_count() == kPerfRequestKillBitCount && writer.finish(written)
           && written == kPerfRequestKillByteCount;
}

/** Decodes one exact message-56 body. */
bool decode_perf_request_kill(std::span<const std::byte> input,
                              PerfRequestKillBody& body) noexcept {
    if (input.size() != kPerfRequestKillByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t raw = 0;
    if (!reader.read(kSigned32Width, raw) || !finish_padding(reader)) {
        return false;
    }
    const PerfRequestKillBody parsed{decode_signed32(raw)};
    body = parsed;
    return true;
}

/** Checks one exact message-56 body. */
bool validate_perf_request_kill(std::span<const std::byte> input) noexcept {
    PerfRequestKillBody body{};
    return decode_perf_request_kill(input, body);
}

/** Encodes one complete message-57 body. */
bool encode_perf_request_reflect(const PerfRequestReflectBody& body,
                                 std::span<std::byte> output,
                                 std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kPerfRequestReflectByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kPerfRequestReflectByteCount));
    return writer.write(body.value ? 1U : 0U, kBoolWidth)
           && writer.bit_count() == kPerfRequestReflectBitCount && writer.finish(written)
           && written == kPerfRequestReflectByteCount;
}

/** Decodes one exact message-57 body. */
bool decode_perf_request_reflect(std::span<const std::byte> input,
                                 PerfRequestReflectBody& body) noexcept {
    if (input.size() != kPerfRequestReflectByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t value = 0;
    if (!reader.read(kBoolWidth, value) || !finish_padding(reader)) {
        return false;
    }
    const PerfRequestReflectBody parsed{value != 0};
    body = parsed;
    return true;
}

/** Checks one exact message-57 body. */
bool validate_perf_request_reflect(std::span<const std::byte> input) noexcept {
    PerfRequestReflectBody body{};
    return decode_perf_request_reflect(input, body);
}

} // namespace sunrise::middleware::bap::activity_message::host_control
