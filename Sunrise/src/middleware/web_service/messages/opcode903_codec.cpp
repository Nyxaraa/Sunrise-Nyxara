#include <cstddef>

#include "../../encoding/bit_reader.h"
#include "opcode903.h"

namespace sunrise::middleware::web_service::messages::opcode903 {
namespace {

/** The reflected opcode-903 request occupies exactly 144 bits. */
constexpr std::size_t kPayloadSize = 18;
/** Optional object identifiers carry one presence bit before the 64-bit SOID. */
constexpr std::uint8_t kInstanceWidth = 64;
/** Signed native definition indices are biased into one 16-bit wire field. */
constexpr std::uint8_t kDefinitionIndexWidth = 15;
/** A signed socket lane is biased from INT32_MIN into one 32-bit wire field. */
constexpr std::uint8_t kSocketIndexWidth = 32;
/** The reflected inner structure is padded to a byte before the outer request fields. */
constexpr std::uint8_t kInnerPaddingWidth = 7;
/** Two absent optional fields terminate the generic Web Service request descriptor. */
constexpr std::uint8_t kOuterTrailerWidth = 2;
/** The complete outer request is padded to its final byte. */
constexpr std::uint8_t kFinalPaddingWidth = 6;
/** Nonnegative signed 32-bit lanes have this bit set after native descriptor biasing. */
constexpr std::uint64_t kSocketIndexBias = 0x80000000ULL;
/** An absent signed 16-bit definition index serializes as biased -1. */
constexpr std::uint64_t kAbsentDefinitionTail = 0x7FFFULL;

} // namespace

/** Parses the complete native socket-plug action descriptor. */
bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode || message.payload.size() != kPayloadSize) {
        return false;
    }

    encoding::bits::Reader reader(message.payload);
    std::uint64_t instancePresent = 0;
    std::uint64_t targetDefinitionPresent = 0;
    std::uint64_t encodedTargetDefinition = 0;
    std::uint64_t encodedSocketIndex = 0;
    std::uint64_t plugDefinitionPresent = 0;
    std::uint64_t encodedPlugDefinition = 0;
    std::uint64_t innerPadding = 0;
    std::uint64_t outerTrailer = 0;
    std::uint64_t finalPadding = 0;
    if (!reader.read(1, instancePresent) || !reader.read(kInstanceWidth, request.instanceSoid)
        || !reader.read(1, targetDefinitionPresent)
        || !reader.read(kDefinitionIndexWidth, encodedTargetDefinition)
        || !reader.read(kSocketIndexWidth, encodedSocketIndex)
        || !reader.read(1, plugDefinitionPresent)
        || !reader.read(kDefinitionIndexWidth, encodedPlugDefinition)
        || !reader.read(kInnerPaddingWidth, innerPadding)
        || !reader.read(kOuterTrailerWidth, outerTrailer)
        || !reader.read(kFinalPaddingWidth, finalPadding) || reader.remaining_bits() != 0
        || innerPadding != 0 || outerTrailer != 0 || finalPadding != 0
        || encodedSocketIndex < kSocketIndexBias) {
        request = {};
        return false;
    }

    request.hasInstance = instancePresent != 0;
    request.hasTargetDefinition = targetDefinitionPresent != 0;
    request.hasPlugDefinition = plugDefinitionPresent != 0;
    request.targetDefinitionIndex = static_cast<std::uint16_t>(encodedTargetDefinition);
    request.plugDefinitionIndex = static_cast<std::uint16_t>(encodedPlugDefinition);
    request.socketIndex = static_cast<std::uint32_t>(encodedSocketIndex - kSocketIndexBias);

    // The signed-index descriptor writes -1 as a clear presence bit plus fifteen set value bits.
    // Requiring that canonical tail prevents a second representation of an absent definition.
    if ((!request.hasTargetDefinition && encodedTargetDefinition != kAbsentDefinitionTail)
        || (!request.hasPlugDefinition && encodedPlugDefinition != kAbsentDefinitionTail)) {
        request = {};
        return false;
    }
    return true;
}

} // namespace sunrise::middleware::web_service::messages::opcode903
