#pragma once
#include "sensor_auth_update.h"

namespace sunrise::middleware::bap::activity_message::sensor_auth_update {
inline bool extend_key_order(std::span<std::uint32_t> order, std::size_t& count,
    std::span<const std::uint32_t> incoming) noexcept {
    if (count > order.size()) return false;
    for (auto key : incoming) {
        bool known = false;
        for (std::size_t i = 0; i < count; ++i) known = known || order[i] == key;
        if (!known) {
            if (count == order.size()) return false;
            order[count++] = key;
        }
    }
    return true;
}
inline bool key_present(const Roster& roster, std::uint32_t key) noexcept {
    for (std::size_t i = 0; i < roster.groupCount; ++i)
        if (roster.groups[i].key == key) return !roster.groups[i].retired;
    return false;
}
// The mask indexes the retained key array. A removed key must not shift its neighbours.
inline std::uint32_t presence_word(const Roster& roster,
    std::span<const std::uint32_t> keys, std::size_t word) noexcept {
    std::uint32_t mask = 0;
    for (std::size_t bit = 0; bit < 32 && word * 32 + bit < keys.size(); ++bit)
        if (key_present(roster, keys[word * 32 + bit])) mask |= std::uint32_t{1} << bit;
    return mask;
}
}
