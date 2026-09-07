#include <array>

#include "../status_fields.h"
#include "family5_codec.h"
#include "opcode205.h"

namespace sunrise::middleware::web_service::messages::opcode205 {
namespace {

/** 905 bytes hold the largest bounded family-5 response, the 64-bit server clock included. */
constexpr std::size_t kMaximumResponseSize = 905;

} // namespace

/** Reports whether this request is the family-5 snapshot. */
bool parse_request(const Message& message) noexcept {
    return message.opcode == kOpcode;
}

/** Encodes status, family-5 State in descriptor order, and cleared trailers. */
bool encode_response(const Message& message,
                     const state::InvestmentState& investment,
                     std::uint64_t serverClockSeconds,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    if (!parse_request(message) || !family5::valid(investment.family5)) {
        return false;
    }
    std::array<std::byte, kMaximumResponseSize> staged{};
    encoding::bits::Writer writer = begin_response(message, staged);
    const bool encoded = status::write_fields(writer, ResponseShape::statusOnly, StatusResponse{})
                         && family5::write(writer, investment.family5, serverClockSeconds);
    return finish_response(writer, encoded, staged, output, written);
}

} // namespace sunrise::middleware::web_service::messages::opcode205
