#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode1820 {

/** Web Service opcode used by Collections to create one item instance. */
inline constexpr std::uint16_t kOpcode = 1820;

/** The one logical field carried by the native Collections pull descriptor. */
struct Request {
    std::uint16_t collectibleIndex{};
};

/**
 * Parses the exact reflected opcode-1820 Collections descriptor.
 * The collectible is optional, so a presence bit precedes its fifteen-bit row. A request naming
 * no collectible is not a pull and is refused here.
 * @param message Parsed Web Service envelope.
 * @param request Receives the named collectible row.
 * @return True only for the complete canonical three-byte request naming a collectible.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace sunrise::middleware::web_service::messages::opcode1820
