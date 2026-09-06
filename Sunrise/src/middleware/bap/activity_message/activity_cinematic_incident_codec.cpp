#include <bit>

#include "../../encoding/bit_reader.h"
#include "cinematic_incident.h"

namespace sunrise::middleware::bap::activity_message::cinematic_incident {
namespace {

namespace bits = encoding::bits;

constexpr std::size_t kClientReferenceOffset = 335;
constexpr std::uint8_t kRegistryKeyBits = 32;
constexpr std::uint8_t kSlotTypeBits = 7;
constexpr std::uint8_t kSlotIndexBits = 16;
constexpr std::uint8_t kRuntimeObjectBits = 64;
constexpr std::uint8_t kEventValueBits = 32;
constexpr std::uint8_t kPaddingBits = 2;
constexpr std::int32_t kSlotTypeBias = 1;
constexpr std::int32_t kSlotIndexBias = 32'768;

} // namespace

/**
 * Maps one global SObject row to its cinematic signal.
 * @param target Global SObject row from the incident header.
 * @param output Set only when the row is one of the three cinematic rows.
 * @return True when the row names a cinematic signal.
 */
bool signal_for_target(std::uint32_t target, Signal& output) noexcept {
    if (target == kStartedTarget) {
        output = Signal::started;
        return true;
    }
    if (target == kTerminatedTarget) {
        output = Signal::terminated;
        return true;
    }
    if (target == kSkipTarget) {
        output = Signal::skipRequested;
        return true;
    }
    return false;
}

/**
 * Decodes the fixed 486-bit payload; the two trailing padding bits must be zero.
 * @param input Exact payload bytes.
 * @param output Cleared first. Receives the ClientRef and runtime values.
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
    std::uint64_t runtimeObjectId = 0;
    std::uint64_t eventValue = 0;
    std::uint64_t padding = 0;
    if (!reader.skip(kClientReferenceOffset) || !reader.read(kRegistryKeyBits, registryKey)
        || !reader.read(kSlotTypeBits, slotType) || !reader.read(kSlotIndexBits, slotIndex)
        || !reader.read(kRuntimeObjectBits, runtimeObjectId)
        || !reader.read(kEventValueBits, eventValue) || !reader.read(kPaddingBits, padding)
        || padding != 0 || reader.remaining_bits() != 0) {
        return false;
    }

    Payload parsed{};
    parsed.registryKey = static_cast<std::uint32_t>(registryKey);
    parsed.slotType = static_cast<std::int8_t>(static_cast<std::int32_t>(slotType) - kSlotTypeBias);
    parsed.slotIndex =
        static_cast<std::int16_t>(static_cast<std::int32_t>(slotIndex) - kSlotIndexBias);
    parsed.runtimeObjectId = runtimeObjectId;
    parsed.eventValue = std::bit_cast<float>(static_cast<std::uint32_t>(eventValue));
    output = parsed;
    return true;
}

} // namespace sunrise::middleware::bap::activity_message::cinematic_incident
