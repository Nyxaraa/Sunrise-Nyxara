#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode2400 {

/** Web Service opcode used to claim one reward from a progression reward list. */
inline constexpr std::uint16_t kOpcode = 2400;

/** Exact pair carried by the native fixed two-element request array. */
struct Request {
    std::uint16_t progressionIndex{};
    std::uint16_t rewardIndex{};
};

/** Parses the complete native opcode-2400 progression/reward pair. */
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace sunrise::middleware::web_service::messages::opcode2400
