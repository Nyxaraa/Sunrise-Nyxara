#include "activity_replication_epoch_encoder.h"

namespace sunrise::middleware::bap::activity_message::replication_epoch {
namespace {

/** The second byte is one abort bit followed by seven zero padding bits. */
constexpr std::uint8_t kAbortMask = 0x80;
constexpr std::uint8_t kPaddingMask = 0x7F;

} // namespace

/** Encodes activity message 44 with the give-up latch byte forced to zero. */
bool encode(std::uint8_t generation, std::span<std::byte> output, std::size_t& written) noexcept {
    return encode_body({generation, false}, output, written);
}

/** Encodes one complete safe message-44 body. */
bool encode_body(const Body& body, std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (body.abort || output.size() < kEncodedSize) {
        return false;
    }
    output[0] = static_cast<std::byte>(body.epoch);
    output[1] = std::byte{};
    written = kEncodedSize;
    return true;
}

/** Decodes one exact message-44 body. */
bool decode(std::span<const std::byte> input, Body& body) noexcept {
    if (input.size() != kEncodedSize
        || (std::to_integer<std::uint8_t>(input[1]) & kPaddingMask) != 0) {
        return false;
    }
    const Body parsed{std::to_integer<std::uint8_t>(input[0]),
                      (std::to_integer<std::uint8_t>(input[1]) & kAbortMask) != 0};
    body = parsed;
    return true;
}

/** Checks one packed message-44 body. */
bool validate(std::span<const std::byte> input) noexcept {
    Body body{};
    return decode(input, body);
}

} // namespace sunrise::middleware::bap::activity_message::replication_epoch
