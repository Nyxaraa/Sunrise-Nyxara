#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../encoding/bit_writer.h"
#include "auth_fields.h"

// Type-65 Ghost-link sensor. Field .0 is a monotonic reset generation, .1 enables the native
// interaction, and .2 keeps the authored interaction hash when it carries the no-name hash.

namespace sunrise::middleware::bap::activity_message::ghost_link {

namespace fields = auth_fields;

inline constexpr std::uint8_t kSlotType = 65;
inline constexpr std::uint32_t kComponentClass = 0x80804D31U;
inline constexpr std::uint32_t kAuthSchema = 0x80804D3FU;
inline constexpr std::uint32_t kSenseSchema = 0x80804D3EU;
inline constexpr std::size_t kBitCount = 65;
inline constexpr std::size_t kByteCount = 9;
/** Field .2 value that preserves the authored interaction. */
inline constexpr std::uint32_t kAuthoredInteraction = fields::kClientRefAbsentKey;

/**
 * Encodes the sensor body.
 * @param generation Positive reset generation.
 * @param output At least kByteCount. @param written Receives kByteCount.
 */
[[nodiscard]] inline bool encode(std::int32_t generation,
                                 bool enabled,
                                 std::span<std::byte> output,
                                 std::size_t& written) noexcept {
    written = 0;
    if (generation <= 0 || output.size() < kByteCount) {
        return false;
    }
    encoding::bits::Writer writer(output.first(kByteCount));
    return writer.write(static_cast<std::uint32_t>(generation) + fields::kSigned32Bias, 32)
           && writer.write(enabled ? 1U : 0U, fields::kBoolWidth)
           && writer.write(kAuthoredInteraction, 32)
           && fields::finish_exact(writer, kBitCount, kByteCount, written);
}

} // namespace sunrise::middleware::bap::activity_message::ghost_link
