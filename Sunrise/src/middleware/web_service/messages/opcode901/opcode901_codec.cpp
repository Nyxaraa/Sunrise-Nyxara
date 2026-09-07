/**
 * Opcode 901 is a vendor purchase. The request carries a vendor index, a sale index and an
 * optional clock. The clock is decoded so the body is checked whole; nothing reads it yet.
 */

#include "opcode901_codec.h"

#include "../../../encoding/bit_reader.h"
#include "../biased_field.h"

namespace sunrise::middleware::web_service::messages::opcode901 {
namespace {

/** The optional clock is a 64-bit signed value with no bias. */
constexpr std::uint8_t kClockWidth = 64;
/** One presence bit precedes the clock. */
constexpr std::uint8_t kPresenceWidth = 1;
/**
 * Bits allowed after the last field. Both legal forms end mid byte, so up to seven bits pad it.
 * A whole byte left over is data, not padding.
 */
constexpr std::size_t kPaddingLimit = 8;

} // namespace

/** Decodes one purchase request body. */
bool parse_request(const Message& message, Request& output) noexcept {
    if (message.opcode != kOpcode) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    Request candidate{};
    std::uint64_t present = 0;
    if (!read_biased_index(reader, candidate.vendorIndex)
        || !read_biased_index(reader, candidate.saleIndex)
        || !reader.read(kPresenceWidth, present)) {
        return false;
    }
    candidate.hasClock = present != 0;
    if (candidate.hasClock) {
        std::uint64_t clock = 0;
        if (!reader.read(kClockWidth, clock)) {
            return false;
        }
        candidate.clock = static_cast<std::int64_t>(clock);
    }
    if (reader.remaining_bits() >= kPaddingLimit) {
        return false;
    }
    output = candidate;
    return true;
}

} // namespace sunrise::middleware::web_service::messages::opcode901
