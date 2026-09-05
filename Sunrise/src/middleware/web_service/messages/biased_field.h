#pragma once

#include <cstdint>

#include "../../encoding/bit_reader.h"

namespace sunrise::middleware::web_service::messages {

/** The vendor opcodes carry their indices as 16-bit signed fields. */
inline constexpr std::uint8_t kBiasedIndexWidth = 16;
/** Their descriptor bias is the signed 16-bit midpoint. */
inline constexpr std::int32_t kBiasedIndexBias = 0x8000;

/**
 * Reads one biased 16-bit index field.
 *
 * Opcodes 901 and 904 both open with fields of this shape, and each codec carried its own copy of
 * the reader. One copy means the two cannot drift.
 *
 * @param reader Open reader.
 * @param output Receives the logical index.
 * @return True when the field was present.
 */
[[nodiscard]] inline bool read_biased_index(encoding::bits::Reader& reader,
                                            std::int16_t& output) noexcept {
    std::uint64_t stored = 0;
    if (!reader.read(kBiasedIndexWidth, stored)) {
        return false;
    }
    output = static_cast<std::int16_t>(static_cast<std::int32_t>(stored) - kBiasedIndexBias);
    return true;
}

} // namespace sunrise::middleware::web_service::messages
