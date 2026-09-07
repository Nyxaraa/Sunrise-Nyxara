#include "activity_message_request_parser.h"

#include "../../encoding/byte_order.h"

namespace sunrise::middleware::bap::activity_message {
namespace {

/** Fixed service-8 fields after the generic 6-byte BAP request header. */
struct RequestLayout final {
    /** The activity session this link owns starts the service body. */
    static constexpr std::size_t sessionId = 0;
    /** The activity envelope follows the big-endian session id. */
    static constexpr std::size_t discriminator = sessionId + encoding::kU64Size;
    /** The big-endian activity message type follows the discriminator. */
    static constexpr std::size_t messageType = discriminator + sizeof(std::byte);
    /** The big-endian payload length follows the activity message type. */
    static constexpr std::size_t payloadLength = messageType + encoding::kU32Size;
    /** The runtime peer-heard mask follows the payload length. */
    static constexpr std::size_t peerHeardMask = payloadLength + encoding::kU32Size;
    /** Compact-arm payload bytes start where the peer-heard mask would begin. */
    static constexpr std::size_t compactPayload = peerHeardMask;
    /** Full-arm payload bytes start after every fixed-width service field. */
    static constexpr std::size_t fullPayload = peerHeardMask + encoding::kU32Size;
};

} // namespace

/** Checks both native service-8 body arms and borrows the exact declared payload. */
bool parse_request(std::span<const std::byte> input, Request& request) noexcept {
    request = {};
    if (input.size() < RequestLayout::compactPayload) {
        return false;
    }

    const std::byte discriminator = input[RequestLayout::discriminator];
    const bool hasPeerHeardMask = discriminator == kTransportDiscriminator;
    if (!hasPeerHeardMask && discriminator != kCompactTransportDiscriminator) {
        return false;
    }
    const std::size_t payloadOffset =
        hasPeerHeardMask ? RequestLayout::fullPayload : RequestLayout::compactPayload;
    if (input.size() < payloadOffset) {
        return false;
    }

    const std::uint32_t declaredLength =
        encoding::read_u32_be(input.subspan<RequestLayout::payloadLength, encoding::kU32Size>());
    const std::size_t availableLength = input.size() - payloadOffset;
    if (declaredLength > kMaximumPayloadSize
        || static_cast<std::size_t>(declaredLength) != availableLength) {
        return false;
    }

    Request parsed{};
    parsed.sessionId =
        encoding::read_u64_be(input.subspan<RequestLayout::sessionId, encoding::kU64Size>());
    parsed.messageType =
        encoding::read_u32_be(input.subspan<RequestLayout::messageType, encoding::kU32Size>());
    parsed.peerHeardMask =
        hasPeerHeardMask ? encoding::read_u32_be(
                               input.subspan<RequestLayout::peerHeardMask, encoding::kU32Size>())
                         : 0;
    parsed.variant =
        hasPeerHeardMask ? RequestVariant::withPeerHeardMask : RequestVariant::withoutPeerHeardMask;
    parsed.payload = input.subspan(payloadOffset, availableLength);
    request = parsed;
    return true;
}

} // namespace sunrise::middleware::bap::activity_message
