#include <cstddef>

#include "../../encoding/bit_reader.h"
#include "opcode1821.h"

namespace sunrise::middleware::web_service::messages::opcode1821 {
namespace {

constexpr std::size_t kPayloadSize = 3;
constexpr std::uint8_t kRecordIndexWidth = 16;
constexpr std::uint8_t kPaddingWidth = 8;
constexpr std::uint64_t kRecordBias = 1ULL << 15U;

} // namespace

bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode || message.payload.size() != kPayloadSize) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    std::uint64_t encodedRecordIndex = 0;
    std::uint64_t padding = 0;
    if (!reader.read(kRecordIndexWidth, encodedRecordIndex)
        || !reader.read(kPaddingWidth, padding) || reader.remaining_bits() != 0 || padding != 0) {
        return false;
    }
    const std::int64_t logicalRecord = static_cast<std::int64_t>(encodedRecordIndex)
                                       - static_cast<std::int64_t>(kRecordBias);
    if (logicalRecord < -1 || logicalRecord > 0x7FFF) {
        return false;
    }
    request.recordIndex = logicalRecord == -1 ? kUnequippedRecordIndex
                                              : static_cast<std::uint16_t>(logicalRecord);
    return true;
}

} // namespace sunrise::middleware::web_service::messages::opcode1821
