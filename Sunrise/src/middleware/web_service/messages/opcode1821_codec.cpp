#include <cstddef>

#include "../../encoding/bit_reader.h"
#include "biased_field.h"
#include "opcode1821.h"

namespace sunrise::middleware::web_service::messages::opcode1821 {
namespace {

/** One biased 16-bit row plus the descriptor's final zero padding byte. */
constexpr std::size_t kPayloadSize = 3;
/** The descriptor pads its two payload bytes out to three. */
constexpr std::uint8_t kPaddingWidth = 8;
/** Logical -1 is the only negative row the screen sends; it clears the title. */
constexpr std::int16_t kUnequippedRow = -1;

} // namespace

/**
 * Parses the biased signed title row carried by opcode 1821.
 * @param request Receives the row; logical -1 becomes kUnequippedRecordIndex.
 * @return False on a wrong opcode, a wrong payload size, or non-zero padding.
 */
bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode || message.payload.size() != kPayloadSize) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    std::int16_t logicalRecord = 0;
    std::uint64_t padding = 0;
    if (!read_biased_index(reader, logicalRecord) || !reader.read(kPaddingWidth, padding)
        || reader.remaining_bits() != 0 || padding != 0 || logicalRecord < kUnequippedRow) {
        return false;
    }
    request.recordIndex = logicalRecord == kUnequippedRow
                              ? kUnequippedRecordIndex
                              : static_cast<std::uint16_t>(logicalRecord);
    return true;
}

} // namespace sunrise::middleware::web_service::messages::opcode1821
