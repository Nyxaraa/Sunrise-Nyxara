#pragma once

#include <cstddef>
#include <cstdint>

#include "../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode702 {

/** Web Service opcode of the character object B write-back. */
inline constexpr std::uint16_t kOpcode = 702;
/** The body is the 5360-byte mirror at objB `+12032`, packed into exactly 4800 bytes. */
inline constexpr std::size_t kPayloadSize = 4800;
/** Value of the world-state field once the client has entered the world. */
inline constexpr std::uint8_t kInWorld = 8;

/** The one field the server acts on. Everything after it is client-owned state it stores. */
struct Request {
    /**
     * Five-bit field at objB `+12068`, schema path `.0.11.1.0.0.4`. Measured 0 on the orbit
     * screen, 1 from the launch through the load, 8 after `activity:in_world`.
     */
    std::uint8_t worldState{};
};

/**
 * Reads the world-state field from the front of a ws-702 body.
 * The 32-byte header and the 5-byte block before the field have fixed widths. So the field sits
 * at bits 293 to 297, whenever every presence bit before it is set.
 * @param message Parsed Web Service envelope.
 * @param request Receives the field.
 * @return True for a complete body whose leading presence bits are all set.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace sunrise::middleware::web_service::messages::opcode702
