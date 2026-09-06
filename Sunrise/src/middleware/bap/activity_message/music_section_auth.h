#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::bap::activity_message::music_section {
inline constexpr std::uint32_t kSchema = 0x80804F58U, kClass = 0x80804E8EU;
inline constexpr std::size_t kBits = 7223, kBytes = (kBits + 7) / 8;
// 1069A10 applies this Auth to the native music sensor; 106AEA0 evaluates it.
// DEF9F0 treats 80809291 as a 128-bit selection mask (NOT four cue hashes),
// followed by 128 optional condition refs. The final ref gates the whole sensor.
// A single enabled authored section lets the native bank perform its transition.
[[nodiscard]] inline bool encode(std::uint8_t section, bool enabled,
    std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (section >= 128 || output.size() < kBytes) return false;
    encoding::bits::Writer w(output.first(kBytes));
    for (std::size_t lane = 0; lane < 4; ++lane)
        if (!w.write(enabled && section / 32 == lane ? std::uint32_t{1} << (section % 32) : 0, 32)) return false;
    for (std::size_t i = 0; i < 129; ++i)
        if (!w.write(0x811C9DC5U, 32) || !w.write(0, 7) || !w.write(0x7FFF, 16)) return false;
    return w.bit_count() == kBits && w.finish(written) && written == kBytes;
}
[[nodiscard]] inline bool validate(std::span<const std::byte> input, std::size_t bits) noexcept {
    if (bits != kBits || input.size() != kBytes) return false;
    encoding::bits::Reader r(input);
    bool enabled{}; std::uint8_t section{};
    for (std::size_t lane = 0; lane < 4; ++lane) {
        std::uint64_t mask{};
        if (!r.read(32, mask)) return false;
        if (mask != 0) {
            if (enabled || (mask & (mask - 1)) != 0) return false;
            enabled = true;
            for (std::size_t bit = 0; bit < 32; ++bit)
                if (mask & (std::uint64_t{1} << bit)) section = static_cast<std::uint8_t>(lane * 32 + bit);
        }
    }
    std::array<std::byte, kBytes> canonical{}; std::size_t written{};
    return encode(section, enabled, canonical, written) && std::equal(input.begin(), input.end(), canonical.begin());
}
}
