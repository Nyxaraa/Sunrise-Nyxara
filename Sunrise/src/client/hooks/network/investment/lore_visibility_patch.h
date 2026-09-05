#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace sunrise::client::hooks::network::investment::lore {
struct Instruction {
    std::uint32_t opcode{}, operand{};
    friend bool operator==(const Instruction&, const Instruction&) = default;
};
enum class Shape { constant, eva, confessions, chronicon };
struct Target {
    std::uint16_t row, field;
    std::uint32_t hash;
    Shape shape;
    bool node;
};
inline constexpr auto kTargets = [] {
    std::array<Target, 36> targets{};
    std::size_t n = 0;
    targets[n++] = {820, 64, 0x13F7E95CU, Shape::eva, true};
    targets[n++] = {837, 64, 0x3FCE8988U, Shape::chronicon, true};
    constexpr std::array<std::uint32_t, 15> wishes{0xFA360CA1U,
                                                   0xFA360CA2U,
                                                   0xFA360CA3U,
                                                   0xFA360CA4U,
                                                   0xFA360CA5U,
                                                   0xFA360CA6U,
                                                   0xFA360CA7U,
                                                   0xFA360CA8U,
                                                   0xFA360CA9U,
                                                   0xFB360E13U,
                                                   0xFB360E12U,
                                                   0xFB360E11U,
                                                   0xFB360E10U,
                                                   0xFB360E17U,
                                                   0xFB360E16U};
    for (std::size_t i = 0; i < wishes.size(); ++i)
        targets[n++] = {
            static_cast<std::uint16_t>(825 + i), 120, wishes[i], Shape::constant, false};
    targets[n++] = {1707, 136, 0xB337A52FU, Shape::confessions, false};
    constexpr std::array<std::uint32_t, 9> chapters{0xB780F393U,
                                                    0xB780F390U,
                                                    0xB780F391U,
                                                    0xB780F396U,
                                                    0xB780F397U,
                                                    0xB780F394U,
                                                    0xB780F395U,
                                                    0xB780F39AU,
                                                    0xB780F39BU};
    for (std::size_t i = 0; i < chapters.size(); ++i) {
        targets[n++] = {
            static_cast<std::uint16_t>(1708 + i), 120, chapters[i], Shape::confessions, false};
        targets[n++] = {
            static_cast<std::uint16_t>(1708 + i), 136, chapters[i], Shape::constant, false};
    }
    return targets;
}();

/** Validate the entire shipped expression before replacing just its first instruction. */
[[nodiscard]] inline bool
replacement(Shape shape, std::span<const Instruction> code, Instruction& output) noexcept {
    if (shape == Shape::constant) {
        if (code.size() != 1 || code[0] != Instruction{11, 1}) return false;
        output = {11, 0}; // false
        return true;
    }
    if (shape == Shape::eva || shape == Shape::confessions) {
        const Instruction read =
            shape == Shape::eva ? Instruction{10, 10343} : Instruction{1, 8702};
        if (code.size() != 2 || code[0] != read || code[1] != Instruction{2, 0}) return false;
        output = {11, 1}; // NOT true = false
        return true;
    }
    if (code.size() != 59) return false;
    for (std::size_t i = 0; i < 15; ++i) {
        if (code[i * 3] != Instruction{10, static_cast<std::uint32_t>(10615 + i)}
            || code[i * 3 + 1] != Instruction{11, 0}
            || code[i * 3 + 2] != Instruction{8, UINT32_MAX})
            return false;
    }
    for (std::size_t i = 45; i < 59; ++i)
        if (code[i] != Instruction{4, UINT32_MAX}) return false;
    output = {11, 1}; // First equality is 1 == 0, making the entire conjunction false.
    return true;
}
} // namespace sunrise::client::hooks::network::investment::lore
