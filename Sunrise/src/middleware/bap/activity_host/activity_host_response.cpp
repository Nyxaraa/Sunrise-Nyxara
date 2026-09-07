#include "activity_host_response.h"

#include <algorithm>
#include <array>

#include "../../encoding/byte_order.h"

namespace sunrise::middleware::bap::activity_host {
namespace {

/** Fixed offsets for the complete svc-16 host request. */
struct RequestLayout final {
    /** Byte offsets from the start of the body. */
    static constexpr std::size_t activityHostId = 0;
    static constexpr std::size_t size = activityHostId + encoding::kU64Size;
};

/** Fixed offsets for the complete svc-17 relay response. */
struct ResponseLayout final {
    /** Byte offsets from the start of the body. */
    static constexpr std::size_t activityHostId = RequestLayout::activityHostId;
    static constexpr std::size_t relayAddress = activityHostId + encoding::kU64Size;
    static constexpr std::size_t neutral = relayAddress + encoding::kU32Size;
    static constexpr std::size_t relayPort = neutral + encoding::kU16Size;
    static constexpr std::size_t size = relayPort + encoding::kU16Size;
};

/** A zero IPv4 address cannot route the activity-host connection. */
constexpr std::uint32_t kInvalidRelayAddress = 0;
/** A zero port cannot route the activity-host connection. */
constexpr std::uint16_t kInvalidRelayPort = 0;
/** Svc 17 needs its middle 16-bit field to stay neutral. */
constexpr std::uint16_t kNeutralField = 0;

static_assert(RequestLayout::size == kRequestBodySize);
static_assert(ResponseLayout::size == kResponseBodySize);

} // namespace

/** Decodes one exact service-16 request. */
bool decode_request(std::span<const std::byte> input, Request& request) noexcept {
    request = {};
    if (input.size() != RequestLayout::size) {
        return false;
    }
    request.activityHostId =
        encoding::read_u64_be(input.subspan<RequestLayout::activityHostId, encoding::kU64Size>());
    return true;
}

/** Encodes one exact service-16 request. */
bool encode_request(const Request& request,
                    std::span<std::byte> output,
                    std::size_t& written) noexcept {
    written = 0;
    if (output.size() < RequestLayout::size) {
        return false;
    }
    std::array<std::byte, RequestLayout::size> body{};
    encoding::write_u64_be(
        std::span(body).subspan<RequestLayout::activityHostId, encoding::kU64Size>(),
        request.activityHostId);
    std::copy(body.begin(), body.end(), output.begin());
    written = body.size();
    return true;
}

/** Decodes one exact service-17 response and validates its routeable endpoint. */
bool decode_response(std::span<const std::byte> input, Response& response) noexcept {
    response = {};
    if (input.size() != ResponseLayout::size) {
        return false;
    }
    Response parsed{};
    parsed.activityHostId =
        encoding::read_u64_be(input.subspan<ResponseLayout::activityHostId, encoding::kU64Size>());
    parsed.relayAddress =
        encoding::read_u32_be(input.subspan<ResponseLayout::relayAddress, encoding::kU32Size>());
    parsed.neutral =
        encoding::read_u16_be(input.subspan<ResponseLayout::neutral, encoding::kU16Size>());
    parsed.relayPort =
        encoding::read_u16_be(input.subspan<ResponseLayout::relayPort, encoding::kU16Size>());
    if (parsed.relayAddress == kInvalidRelayAddress || parsed.neutral != kNeutralField
        || parsed.relayPort == kInvalidRelayPort) {
        return false;
    }
    response = parsed;
    return true;
}

/** Encodes one complete validated service-17 response. */
bool encode_response(const Response& response,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    if (response.relayAddress == kInvalidRelayAddress || response.neutral != kNeutralField
        || response.relayPort == kInvalidRelayPort || output.size() < ResponseLayout::size) {
        return false;
    }

    std::array<std::byte, ResponseLayout::size> body{};
    encoding::write_u64_be(
        std::span(body).subspan<ResponseLayout::activityHostId, encoding::kU64Size>(),
        response.activityHostId);
    encoding::write_u32_be(
        std::span(body).subspan<ResponseLayout::relayAddress, encoding::kU32Size>(),
        response.relayAddress);
    encoding::write_u16_be(std::span(body).subspan<ResponseLayout::neutral, encoding::kU16Size>(),
                           response.neutral);
    encoding::write_u16_be(std::span(body).subspan<ResponseLayout::relayPort, encoding::kU16Size>(),
                           response.relayPort);
    std::copy(body.begin(), body.end(), output.begin());
    written = body.size();
    return true;
}

/** Checks one svc-16 host identity and encodes its svc-17 relay endpoint. */
bool encode_response(std::span<const std::byte> requestBody,
                     std::uint32_t relayAddress,
                     std::uint16_t relayPort,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    Request request{};
    if (!decode_request(requestBody, request)) {
        return false;
    }
    const Response response{request.activityHostId, relayAddress, kNeutralField, relayPort};
    return encode_response(response, output, written);
}

} // namespace sunrise::middleware::bap::activity_host
