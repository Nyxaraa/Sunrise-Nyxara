#pragma once

#include <cstdint>

#include "../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode1821 {

/** Web Service opcode the Seals screen uses to equip one earned title. */
inline constexpr std::uint16_t kOpcode = 1821;

/** Row the request names, or kUnequippedRecordIndex to clear the title. */
inline constexpr std::uint16_t kUnequippedRecordIndex = 0xFFFFU;

/** The one logical field carried by the native title equip descriptor. */
struct Request {
    std::uint16_t recordIndex{};
};

/**
 * Parses the exact biased signed title row carried by opcode 1821.
 * Logical -1 becomes kUnequippedRecordIndex and clears the current title.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& request) noexcept;

} // namespace sunrise::middleware::web_service::messages::opcode1821
