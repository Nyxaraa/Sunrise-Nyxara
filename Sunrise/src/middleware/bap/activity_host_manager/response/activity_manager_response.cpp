#include "activity_manager_response.h"

#include <algorithm>
#include <array>

#include "../../../encoding/byte_order.h"

namespace sunrise::middleware::bap::activity_host_manager::response {
namespace {

/** Fixed offsets in the exact service-7 response body. */
struct ResponseLayout final {
    /** Byte offsets from the start of the body. */
    static constexpr std::size_t discriminator = 0;
    static constexpr std::size_t sessionId = discriminator + sizeof(std::byte);
    static constexpr std::size_t activityData = sessionId + encoding::kU64Size;
    static constexpr std::size_t size = activityData + kActivityDataSize;
};

/** Service 7 rejects a response body with discriminator zero or any value other than 2. */
constexpr std::byte kResponseDiscriminator{2};
/** A zero session id does not move the activity-host request on. */
constexpr std::uint64_t kInvalidSessionId = 0;

static_assert(ResponseLayout::size == kResponseBodySize);

} // namespace

/** Decodes one exact service-7 response and retains the opaque activity data. */
bool decode_response(std::span<const std::byte> input, Response& response) noexcept {
    response = {};
    if (input.size() != ResponseLayout::size
        || input[ResponseLayout::discriminator] != kResponseDiscriminator) {
        return false;
    }
    Response parsed{};
    parsed.sessionId =
        encoding::read_u64_be(input.subspan<ResponseLayout::sessionId, encoding::kU64Size>());
    if (parsed.sessionId == kInvalidSessionId) {
        return false;
    }
    std::copy_n(input.begin() + ResponseLayout::activityData,
                parsed.activityData.size(),
                parsed.activityData.begin());
    response = parsed;
    return true;
}

/** Encodes one complete service-7 response losslessly. */
bool encode_response(const Response& response,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    if (response.sessionId == kInvalidSessionId || output.size() < ResponseLayout::size) {
        return false;
    }

    std::array<std::byte, ResponseLayout::size> body{};
    body[ResponseLayout::discriminator] = kResponseDiscriminator;
    encoding::write_u64_be(std::span(body).subspan<ResponseLayout::sessionId, encoding::kU64Size>(),
                           response.sessionId);
    std::copy(response.activityData.begin(),
              response.activityData.end(),
              body.begin() + ResponseLayout::activityData);
    std::copy(body.begin(), body.end(), output.begin());
    written = body.size();
    return true;
}

/** Encodes a service-7 response with the compatibility zero activity-data policy. */
bool encode_response(std::uint64_t sessionId,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    Response response{};
    response.sessionId = sessionId;
    return encode_response(response, output, written);
}

} // namespace sunrise::middleware::bap::activity_host_manager::response
