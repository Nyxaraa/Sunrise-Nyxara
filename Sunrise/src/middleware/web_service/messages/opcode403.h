#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode403 {

/** Web Service opcode used by the Character screen's Equip action. */
inline constexpr std::uint16_t kOpcode = 403;
/** Web Service opcode used by the Character screen's Unequip action. */
inline constexpr std::uint16_t kUnequipOpcode = 404;

/** The one logical field carried by the shared equip and unequip descriptor. */
struct Request {
    std::uint64_t instanceSoid{};
};

/**
 * Parses the exact shared opcode-403 and opcode-404 item-instance descriptor.
 * Both actions name one item instance and differ only in direction, so they share it. The
 * trailing byte is a required alignment pad and has to be clear.
 * @param message Parsed Web Service envelope.
 * @param request Receives the named item-instance identity.
 * @return True only for the complete canonical nine-byte request naming a nonzero instance.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace sunrise::middleware::web_service::messages::opcode403
