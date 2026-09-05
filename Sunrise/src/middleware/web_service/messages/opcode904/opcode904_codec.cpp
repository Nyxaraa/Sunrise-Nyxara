/**
 * Opcode 904 acquires a quest or other pursuit from a vendor.
 *
 * Captured requests settle the layout: three 16-bit fields biased by `0x8000` - the vendor, the
 * clicked UI slot, and a field zero in every capture - then one 32-bit field biased by `0x80000000`
 * naming the sale row, then one trailing byte skipped rather than guessed at.
 *
 * | payload | vendor | slot | third | sale row |
 * |---|---|---|---|---|
 * | `8016 801C 8000 80000104 00` | 22 | 28 | 0 | 260 |
 * | `8016 801D 8000 800000C3 00` | 22 | 29 | 0 | 195 |
 * | `8016 8020 8000 80000109 00` | 22 | 32 | 0 | 265 |
 * | `8016 8015 8000 7FFFFFFF 00` | 22 | 21 | 0 | -1  |
 *
 * The last row fixes the width: `7FFFFFFF` is -1 under the 32-bit bias, the same absent marker a
 * sale row's own category index carries. Read as a bare 16-bit field it would be 65535.
 */

#include "opcode904_codec.h"

#include "../../../encoding/bit_reader.h"
#include "../biased_field.h"

namespace sunrise::middleware::web_service::messages::opcode904 {
namespace {

/** The sale row is a 32-bit signed value. */
constexpr std::uint8_t kSaleIndexWidth = 32;
/** Its bias is the signed 32-bit midpoint, which is the same rule one width up. */
constexpr std::int64_t kSaleIndexBias = 0x80000000;

} // namespace

/** Decodes one quest-acquire request body. */
bool parse_request(const Message& message, Request& output) noexcept {
    if (message.opcode != kOpcode) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    Request candidate{};
    if (!read_biased_index(reader, candidate.vendorIndex)
        || !read_biased_index(reader, candidate.slotIndex)
        || !read_biased_index(reader, candidate.third)) {
        return false;
    }
    std::uint64_t saleIndex = 0;
    if (reader.read(kSaleIndexWidth, saleIndex)) {
        candidate.saleIndex =
            static_cast<std::int32_t>(static_cast<std::int64_t>(saleIndex) - kSaleIndexBias);
        candidate.hasSaleIndex = true;
    }
    output = candidate;
    return true;
}

} // namespace sunrise::middleware::web_service::messages::opcode904
