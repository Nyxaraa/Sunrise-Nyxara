#include <array>

#include "../../encoding/byte_order.h"
#include "../status_fields.h"
#include "family5_codec.h"
#include "opcode503.h"

namespace sunrise::middleware::web_service::messages::opcode503 {
namespace {

/** Request field 0 is a fixed 36-byte array the Client builds and we never read. */
constexpr std::size_t kRequestPrefixSize = 36;
/** The whole request is the opaque prefix plus the 64-bit account key of field 1. */
constexpr std::size_t kRequestPayloadSize = kRequestPrefixSize + encoding::kU64Size;
/** A zero SOID cannot key the account records built after this response. */
constexpr std::uint64_t kInvalidSoid = 0;
/** Required but unused top-level integer fields still take 32 bits each. */
constexpr std::uint8_t kTopLevelIntegerWidth = 32;
/** Unused required integer fields use their neutral logical value. */
constexpr std::uint32_t kUnusedIntegerValue = 0;
/** Account object ids are 64 wire bits. */
constexpr std::uint8_t kSoidWidth = 64;
/** 925 bytes hold the largest bounded account bootstrap response, the 64-bit clock included. */
constexpr std::size_t kMaximumResponseSize = 925;

} // namespace

/** Parses the account key that sits behind the request's opaque 36-byte prefix. */
bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode) {
        return false;
    }
    if (message.payload.size() < kRequestPayloadSize) {
        return true;
    }
    // The descriptor walk writes the prefix byte-aligned, so the key starts on a byte boundary.
    request.primarySoid =
        encoding::read_u64_be(message.payload.subspan<kRequestPrefixSize, encoding::kU64Size>());
    request.hasPrimarySoid = request.primarySoid != kInvalidSoid;
    return true;
}

/** Encodes account and family-5 State in descriptor order, plus cleared trailers. */
bool encode_response(const Message& message,
                     const Request& request,
                     const state::InvestmentState& investment,
                     std::uint64_t serverClockSeconds,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    if (message.opcode != kOpcode || !family5::valid(investment.family5)) {
        return false;
    }
    std::array<std::byte, kMaximumResponseSize> staged{};
    encoding::bits::Writer writer = begin_response(message, staged);
    // The bootstrap publishes no family-4 revision, so the version wait gets its no-wait value.
    StatusResponse status{};
    status.value = kNoFamily4Publication;
    const bool encoded = status::write_fields(writer, ResponseShape::statusPair, status)
                         && writer.write(request.primarySoid, kSoidWidth)
                         && writer.write(kUnusedIntegerValue, kTopLevelIntegerWidth)
                         && writer.write(kUnusedIntegerValue, kTopLevelIntegerWidth)
                         && family5::write(writer, investment.family5, serverClockSeconds);
    return finish_response(writer, encoded, staged, output, written);
}

} // namespace sunrise::middleware::web_service::messages::opcode503
