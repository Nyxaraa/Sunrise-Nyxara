#include <cstddef>

#include "../../encoding/bit_reader.h"
#include "opcode702.h"

namespace sunrise::middleware::web_service::messages::opcode702 {
namespace {

/** Each optional field or array opens with one presence bit. */
constexpr std::uint8_t kPresenceWidth = 1;
/** The header's three floats at `+12032`, each behind its own presence bit. */
constexpr std::size_t kHeaderFloatCount = 3;
constexpr std::uint8_t kFloatWidth = 32;
/** The four seen-message words at `+12044`, one presence bit for the whole array. */
constexpr std::size_t kSeenWordCount = 4;
constexpr std::uint8_t kSeenWordWidth = 32;
/** Three presence bits open the body, its first struct and the 5-byte block. */
constexpr std::size_t kBodyPresenceCount = 3;
/** The block's three 8-bit fields at `+12064`, then its 3-bit field at `+12067`. */
constexpr std::size_t kBlockByteCount = 3;
constexpr std::uint8_t kBlockByteWidth = 8;
constexpr std::uint8_t kBlockTriadWidth = 3;
/** The world-state field at `+12068`. */
constexpr std::uint8_t kWorldStateWidth = 5;

/** Reads one presence bit and requires it set, so the fixed layout behind it holds. */
bool read_present(encoding::bits::Reader& reader) noexcept {
    std::uint64_t present = 0;
    return reader.read(kPresenceWidth, present) && present != 0;
}

/** Skips a field whose value the server does not read. */
bool skip_field(encoding::bits::Reader& reader, std::uint8_t width) noexcept {
    return reader.skip(width);
}

} // namespace

/** Reads the world-state field from the front of a ws-702 body. */
bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode || message.payload.size() != kPayloadSize) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    bool read = read_present(reader) && read_present(reader);
    for (std::size_t index = 0; read && index < kHeaderFloatCount; ++index) {
        read = read_present(reader) && skip_field(reader, kFloatWidth);
    }
    read = read && read_present(reader);
    for (std::size_t index = 0; read && index < kSeenWordCount; ++index) {
        read = skip_field(reader, kSeenWordWidth);
    }
    read = read && read_present(reader) && skip_field(reader, kFloatWidth);
    for (std::size_t index = 0; read && index < kBodyPresenceCount; ++index) {
        read = read_present(reader);
    }
    for (std::size_t index = 0; read && index < kBlockByteCount; ++index) {
        read = skip_field(reader, kBlockByteWidth);
    }
    std::uint64_t worldState = 0;
    read =
        read && skip_field(reader, kBlockTriadWidth) && reader.read(kWorldStateWidth, worldState);
    if (!read) {
        return false;
    }
    request.worldState = static_cast<std::uint8_t>(worldState);
    return true;
}

} // namespace sunrise::middleware::web_service::messages::opcode702
