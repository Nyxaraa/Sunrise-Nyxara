#include <cstddef>

#include "../../encoding/bit_reader.h"
#include "opcode2400.h"

namespace sunrise::middleware::web_service::messages::opcode2400 {
namespace {

/** Two 16-bit array elements plus the descriptor's final zero padding byte. */
constexpr std::size_t kPayloadSize = 5;
constexpr std::uint8_t kIndexWidth = 16;
constexpr std::uint8_t kPaddingWidth = 8;
constexpr std::uint16_t kIndexBias = 0x8000U;

} // namespace

bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode || message.payload.size() != kPayloadSize) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    std::uint64_t storedProgression = 0;
    std::uint64_t storedReward = 0;
    std::uint64_t padding = 0;
    if (!reader.read(kIndexWidth, storedProgression) || !reader.read(kIndexWidth, storedReward)
        || !reader.read(kPaddingWidth, padding) || reader.remaining_bits() != 0 || padding != 0
        || storedProgression < kIndexBias || storedReward < kIndexBias) {
        return false;
    }
    request.progressionIndex = static_cast<std::uint16_t>(storedProgression - kIndexBias);
    request.rewardIndex = static_cast<std::uint16_t>(storedReward - kIndexBias);
    return true;
}

} // namespace sunrise::middleware::web_service::messages::opcode2400
