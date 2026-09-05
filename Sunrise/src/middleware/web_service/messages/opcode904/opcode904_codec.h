#pragma once

#include <cstdint>

#include "../../web_service_envelope.h"

namespace sunrise::middleware::web_service::messages::opcode904 {

/** Web Service opcode for acquiring a quest or other pursuit from a vendor. */
inline constexpr std::uint16_t kOpcode = 904;

/**
 * One decoded quest-acquire request.
 *
 * Structurally a sibling of the opcode-901 purchase: 16-bit fields biased by `0x8000`. It carries
 * three of them and no clock, where 901 carries two and an optional clock, and then one 32-bit
 * field biased by `0x80000000` that names the sale row.
 */
struct Request {
    /** Index into the vendor table, the same table 901 indexes. */
    std::int16_t vendorIndex{};
    /**
     * UI slot the click landed on. Not a sale row: indexing sale rows with it grants armour mods.
     */
    std::int16_t slotIndex{};
    /** Third field. Zero in every captured request; role open. */
    std::int16_t third{};
    /**
     * Sale row of the vendor definition, as a 32-bit field biased by `0x80000000`. `7FFFFFFF` is
     * -1, the same absent marker a sale row's own category index uses; only the full width reads
     * it as such.
     */
    std::int32_t saleIndex{};
    /** True when the body carried the sale-row field. */
    bool hasSaleIndex{};
};

/**
 * Decodes one quest-acquire request body.
 *
 * Unlike 901 this does not refuse a body with a byte to spare: a captured request is 11 bytes and
 * the four fields account for ten. The trailing byte is skipped rather than read as a field.
 *
 * @param message Parsed Web Service envelope.
 * @param output Receives the request only when the three leading fields decode.
 * @return True when the opcode matches and those fields are present.
 */
[[nodiscard]] bool parse_request(const Message& message, Request& output) noexcept;

} // namespace sunrise::middleware::web_service::messages::opcode904
