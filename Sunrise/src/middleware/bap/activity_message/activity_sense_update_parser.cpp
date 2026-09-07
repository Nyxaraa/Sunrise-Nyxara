/** Prefix-only sensor decode. Reads the epoch and first object without resolving a schema. */

#include "../../encoding/bit_reader.h"
#include "../../encoding/byte_order.h"
#include "sense_update.h"

namespace sunrise::middleware::bap::activity_message::sense_update {
namespace {

/** Field widths of the first object header, in bits. */
constexpr std::uint8_t kPresenceWidth = 1;
constexpr std::uint8_t kKeyWidth = 32;
constexpr std::uint8_t kSlotTypeWidth = 7;
constexpr std::uint8_t kSlotIndexWidth = 16;
/** The slot type and index are sent unsigned, biased so their absent value is zero. */
constexpr std::uint32_t kSlotTypeBias = 1;
constexpr std::uint32_t kSlotIndexBias = 32768;
/** Bits the first object header spends before the Sense body starts. */
constexpr std::size_t kFirstObjectHeaderBits =
    kPresenceWidth + kKeyWidth + kSlotTypeWidth + kSlotIndexWidth;

/**
 * Recovers only the first exact ClientRef when the root delta is empty. The group length is the
 * resynchronization boundary; no Sense bits are retained or interpreted here. A later SDK-backed
 * typed schema walker uses this same reference when a resolver is available.
 */
void parse_first_object(encoding::bits::Reader reader, SenseUpdate& update) noexcept {
    std::uint64_t root = 0;
    std::uint64_t groupPresent = 0;
    std::uint64_t groupKey = 0;
    std::uint64_t groupBits = 0;
    if (!reader.read(kPresenceWidth, root) || root != 0
        || !reader.read(kPresenceWidth, groupPresent) || groupPresent == 0
        || !reader.read(kKeyWidth, groupKey) || !reader.read(kKeyWidth, groupBits)
        || groupBits < kFirstObjectHeaderBits
        || groupBits > kGroupByteCapacity * encoding::kBitsPerByte
        || groupBits > reader.remaining_bits()) {
        return;
    }

    std::uint64_t objectPresent = 0;
    std::uint64_t objectKey = 0;
    std::uint64_t encodedType = 0;
    std::uint64_t encodedIndex = 0;
    if (!reader.read(kPresenceWidth, objectPresent) || objectPresent == 0
        || !reader.read(kKeyWidth, objectKey) || objectKey != groupKey
        || !reader.read(kSlotTypeWidth, encodedType) || encodedType < kSlotTypeBias
        || !reader.read(kSlotIndexWidth, encodedIndex) || encodedIndex < kSlotIndexBias) {
        return;
    }

    update.firstGroupBits = static_cast<std::uint32_t>(groupBits);
    update.firstRegistryKey = static_cast<std::uint32_t>(groupKey);
    update.firstSlotType = static_cast<std::uint8_t>(encodedType - kSlotTypeBias);
    update.firstSlotIndex = static_cast<std::uint16_t>(encodedIndex - kSlotIndexBias);
    update.hasFirstObject = true;
}

} // namespace

/** Parses a sensor sense update as far as its known grammar reaches. */
bool parse_sense_update(std::span<const std::byte> input,
                        SenseUpdate& update,
                        std::size_t& consumedBits) noexcept {
    update = {};
    consumedBits = 0;
    if (input.size() > kOuterByteCapacity) {
        return false;
    }
    encoding::bits::Reader reader(input);
    std::uint64_t literal = 0;
    if (!reader.read(kEpochFieldWidth, update.epoch.first)
        || !reader.read(kEpochFieldWidth, update.epoch.second)
        || !reader.read(kLiteralZeroWidth, literal)) {
        update = {};
        return false;
    }
    consumedBits = input.size() * encoding::kBitsPerByte - reader.remaining_bits();
    if (literal != 0) {
        // The bit is a schema literal, so a set bit means this body is not the shape above and no
        // field read from it can be trusted.
        update = {};
        return false;
    }
    update.tailBits = static_cast<std::uint32_t>(reader.remaining_bits());
    parse_first_object(reader, update);
    return true;
}

} // namespace sunrise::middleware::bap::activity_message::sense_update
