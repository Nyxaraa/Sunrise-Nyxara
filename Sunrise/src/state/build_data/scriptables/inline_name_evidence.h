#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::state::build_data::scriptables::inline_name_evidence {

/** @return The authored 32-bit FNV-1 hash of one exact byte string. */
[[nodiscard]] inline std::uint32_t hash(std::span<const std::byte> bytes) noexcept {
    // FNV-1 32-bit offset basis and prime, as the packages hash their names.
    constexpr std::uint32_t kBasis = 0x811C9DC5U;
    constexpr std::uint32_t kPrime = 0x01000193U;
    std::uint32_t output = kBasis;
    for (const std::byte byte : bytes) {
        output = (output * kPrime) ^ std::to_integer<std::uint8_t>(byte);
    }
    return output;
}

/** @return True when the whole nonempty byte string is canonical UTF-8. */
[[nodiscard]] inline bool valid_utf8(std::span<const std::byte> bytes) noexcept {
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
        const std::uint8_t first = std::to_integer<std::uint8_t>(bytes[cursor]);
        if (first <= 0x7FU) {
            ++cursor;
            continue;
        }

        std::size_t continuationCount = 0;
        std::uint32_t codePoint = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xC2U && first <= 0xDFU) {
            continuationCount = 1;
            codePoint = first & 0x1FU;
            minimum = 0x80U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            continuationCount = 2;
            codePoint = first & 0x0FU;
            minimum = 0x800U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            continuationCount = 3;
            codePoint = first & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (continuationCount > bytes.size() - cursor - 1) {
            return false;
        }
        for (std::size_t index = 0; index < continuationCount; ++index) {
            const std::uint8_t next = std::to_integer<std::uint8_t>(bytes[cursor + index + 1]);
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }
        if (codePoint < minimum || codePoint > 0x10FFFFU
            || (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
            return false;
        }
        cursor += continuationCount + 1;
    }
    return !bytes.empty();
}

} // namespace sunrise::state::build_data::scriptables::inline_name_evidence
