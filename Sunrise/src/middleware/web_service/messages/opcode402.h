#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode402 {

/** Web Service opcode used by the Character screen's Dismantle action. */
inline constexpr std::uint16_t kOpcode = 402;
/** The descriptor only ever names a single unit, whatever the stack holds. */
inline constexpr std::uint32_t kSingleQuantity = 1;

/** Exact logical fields carried by the native 128-bit dismantle descriptor. */
struct Request {
    std::uint64_t instanceSoid{};
    std::uint16_t definitionIndex{};
};

/**
 * Parses the exact reflected opcode-402 dismantle descriptor.
 * The quantity, the required flag, and all three pad runs are fixed for this action.
 * Fields are filled as far as the parse reaches, so a refused request still describes itself.
 * @param message Parsed Web Service envelope.
 * @param request Receives the named instance and its definition row.
 * @return True only for the complete canonical 16-byte single-unit request.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace sunrise::middleware::web_service::messages::opcode402
