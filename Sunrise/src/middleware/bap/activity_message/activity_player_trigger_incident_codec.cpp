#include "../../encoding/bit_reader.h"
#include "player_trigger_incident.h"

namespace sunrise::middleware::bap::activity_message::player_trigger_incident {
namespace {

namespace bits = encoding::bits;

/** Bit offset of the client reference inside the incident body. */
constexpr std::size_t kClientReferenceOffset = 335;
/** Field widths of the client reference and the trigger tail, in bits. */
constexpr std::uint8_t kRegistryKeyBits = 32;
constexpr std::uint8_t kSlotTypeBits = 7;
constexpr std::uint8_t kSlotIndexBits = 16;
constexpr std::uint8_t kResolvedObjectBits = 32;
constexpr std::uint8_t kPaddingBits = 2;
/** The slot type and index are sent unsigned, biased so their absent value is zero. */
constexpr std::int32_t kSlotTypeBias = 1;
constexpr std::int32_t kSlotIndexBias = 32'768;

} // namespace

/**
 * Decodes the fixed 422-bit payload; the two trailing padding bits must be zero.
 * @param input Exact payload bytes.
 * @param output Cleared first. Receives the ClientRef and resolved target id.
 * @return True when the whole payload decoded.
 */
bool decode(std::span<const std::byte> input, Payload& output) noexcept {
    output = {};
    if (input.size() != kPayloadBytes) {
        return false;
    }

    bits::Reader reader(input);
    std::uint64_t registryKey = 0;
    std::uint64_t slotType = 0;
    std::uint64_t slotIndex = 0;
    std::uint64_t resolvedObjectId = 0;
    std::uint64_t padding = 0;
    if (!reader.skip(kClientReferenceOffset) || !reader.read(kRegistryKeyBits, registryKey)
        || !reader.read(kSlotTypeBits, slotType) || !reader.read(kSlotIndexBits, slotIndex)
        || !reader.read(kResolvedObjectBits, resolvedObjectId)
        || !reader.read(kPaddingBits, padding) || padding != 0 || reader.remaining_bits() != 0) {
        return false;
    }

    Payload parsed{};
    parsed.registryKey = static_cast<std::uint32_t>(registryKey);
    parsed.slotType = static_cast<std::int8_t>(static_cast<std::int32_t>(slotType) - kSlotTypeBias);
    parsed.slotIndex =
        static_cast<std::int16_t>(static_cast<std::int32_t>(slotIndex) - kSlotIndexBias);
    parsed.resolvedObjectId = static_cast<std::uint32_t>(resolvedObjectId);
    output = parsed;
    return true;
}

} // namespace sunrise::middleware::bap::activity_message::player_trigger_incident
