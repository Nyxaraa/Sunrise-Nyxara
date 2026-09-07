#include <algorithm>

#include "../../encoding/byte_order.h"
#include "start_activity_host_response.h"

namespace sunrise::middleware::bap::activity_message::start_activity_host_response {

/** Encodes one nonzero session id and the complete 128-byte instance-name field. */
bool encode(const Body& body, std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (body.sessionId == 0 || output.size() < kByteCount) {
        return false;
    }
    const std::span encoded = output.first(kByteCount);
    encoding::write_u64_be(encoded.first<kSessionIdBytes>(), body.sessionId);
    std::copy(body.hostInstanceName.begin(),
              body.hostInstanceName.end(),
              encoded.begin() + kSessionIdBytes);
    written = kByteCount;
    return true;
}

/** Decodes one exact 136-byte body and rejects a zero session id. */
bool decode(std::span<const std::byte> input, Body& body) noexcept {
    if (input.size() != kByteCount) {
        return false;
    }
    Body parsed{};
    parsed.sessionId = encoding::read_u64_be(input.first<kSessionIdBytes>());
    if (parsed.sessionId == 0) {
        return false;
    }
    std::copy(input.begin() + kSessionIdBytes, input.end(), parsed.hostInstanceName.begin());
    body = parsed;
    return true;
}

/** Checks one exact message-10 body without retaining decoded state. */
bool validate(std::span<const std::byte> input) noexcept {
    Body body{};
    return decode(input, body);
}

} // namespace sunrise::middleware::bap::activity_message::start_activity_host_response
