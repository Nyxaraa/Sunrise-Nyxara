#pragma once

#include <cstddef>
#include <cstdint>

#include "../../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode901 {

/** Web Service opcode for a vendor purchase. */
inline constexpr std::uint16_t kOpcode = 901;

/** One decoded purchase request. The two indices are its only identity. */
struct Request {
    /** Index into the vendor table. */
    std::int16_t vendorIndex{};
    /** Index into that vendor's sale rows. */
    std::int16_t saleIndex{};
    /** Client clock in Unix seconds. Decoded so the body is checked whole; no rule reads it. */
    std::int64_t clock{};
    /** False for the absent form, which the decoder still accepts. */
    bool hasClock{};
};

/**
 * Decodes one purchase request body.
 * A body with a whole byte left over after the last field is refused.
 * @param message Parsed Web Service envelope.
 * @param output Receives the request only when the whole body decodes.
 * @return True when the opcode matches and the body is one of the two legal forms.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& output) noexcept;

} // namespace sunrise::middleware::web_service::messages::opcode901
