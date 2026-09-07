/**
 * Opcode 601 is a loot pickup. The Client sends it when it cannot pick the loot up on its own.
 * The reply is 165 bits and no field can be left out, so the bare echo under-runs the decoder.
 * Nothing reads the three tail fields. The request carries no completion handle.
 */

#include "opcode601_codec.h"

#include <array>

#include "../../status_fields.h"

namespace sunrise::middleware::web_service::messages::opcode601 {
namespace {

/** The three tail integers after the status pair are 32, 64 and 32 bits. */
constexpr std::uint8_t kTailIntegerWidth = 32;
constexpr std::uint8_t kTailLongWidth = 64;
/** Required fields nothing reads still take their width. Zero is the neutral value. */
constexpr std::uint64_t kUnusedValue = 0;

} // namespace

/** Reports whether this request is the remote loot pickup. */
bool parse_request(const Message& message) noexcept {
    return message.opcode == kOpcode;
}

/** Encodes the status pair and its three-field tail in descriptor order. */
bool encode_response(const Message& message,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    if (!parse_request(message)) {
        return false;
    }

    std::array<std::byte, kResponseSize> staged{};
    encoding::bits::Writer writer = begin_response(message, staged);
    // The status value is the Family-4 version the Client waits for. This route publishes no
    // revision, so it carries the no-publication value and the Client's wait completes at once.
    StatusResponse status{};
    status.value = kNoFamily4Publication;
    const bool encoded = status::write_fields(writer, ResponseShape::statusPair, status)
                         && writer.write(kUnusedValue, kTailIntegerWidth)
                         && writer.write(kUnusedValue, kTailLongWidth)
                         && writer.write(kUnusedValue, kTailIntegerWidth);
    return finish_response(writer, encoded, staged, output, written);
}

} // namespace sunrise::middleware::web_service::messages::opcode601
