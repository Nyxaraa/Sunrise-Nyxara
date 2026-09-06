#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::bap::activity_message::ghost_link {
inline constexpr std::uint8_t kSlotType = 65;
inline constexpr std::uint32_t kComponentClass = 0x80804D31U;
inline constexpr std::uint32_t kAuthSchema = 0x80804D3FU;
inline constexpr std::uint32_t kSenseSchema = 0x80804D3EU;
inline constexpr std::size_t kBitCount = 65;
inline constexpr std::size_t kByteCount = 9;
inline constexpr std::uint32_t kAuthoredInteraction = 0x811C9DC5U;

// E4DFB0: +0 is a monotonic reset generation, +4 enables native interaction,
// and +8 preserves the authored interaction hash when set to the empty FNV hash.
[[nodiscard]] inline bool encode(std::int32_t generation, bool enabled,
                                 std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (generation <= 0 || output.size() < kByteCount) return false;
    encoding::bits::Writer writer(output.first(kByteCount));
    return writer.write(static_cast<std::uint32_t>(generation) + 0x80000000U, 32)
           && writer.write(enabled ? 1U : 0U, 1)
           && writer.write(kAuthoredInteraction, 32)
           && writer.bit_count() == kBitCount && writer.finish(written) && written == kByteCount;
}
[[nodiscard]] inline bool validate(std::span<const std::byte> input, std::size_t bits) noexcept {
    if (input.size() != kByteCount || bits != kBitCount
        || (std::to_integer<unsigned>(input.back()) & 0x7FU) != 0) return false;
    encoding::bits::Reader reader(input);
    std::uint64_t generation = 0, enabled = 0, interaction = 0;
    return reader.read(32, generation) && generation > 0x80000000ULL
           && reader.read(1, enabled) && reader.read(32, interaction)
           && interaction == kAuthoredInteraction;
}
} // namespace sunrise::middleware::bap::activity_message::ghost_link
