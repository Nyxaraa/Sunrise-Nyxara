#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "sensor_auth_update.h"

namespace sunrise::middleware::bap::activity_message::sensor_auth_update {

/** One presence word covers 32 retained keys. */
inline constexpr std::size_t kPresenceWordBits = 32;

/** @return True when the roster holds the key and has not retired it. */
[[nodiscard]] inline bool key_present(const Roster& roster, std::uint32_t key) noexcept {
    for (std::size_t index = 0; index < roster.groupCount; ++index) {
        if (roster.groups[index].key == key) {
            return !roster.groups[index].retired;
        }
    }
    return false;
}

/**
 * Builds one presence word over the retained key array. The mask indexes that array, so a
 * retired key clears its bit and never shifts its neighbours.
 */
[[nodiscard]] inline std::uint32_t presence_word(const Roster& roster,
                                                 std::span<const std::uint32_t> keys,
                                                 std::size_t word) noexcept {
    std::uint32_t mask = 0;
    for (std::size_t bit = 0;
         bit < kPresenceWordBits && word * kPresenceWordBits + bit < keys.size();
         ++bit) {
        if (key_present(roster, keys[word * kPresenceWordBits + bit])) {
            mask |= std::uint32_t{1} << bit;
        }
    }
    return mask;
}

} // namespace sunrise::middleware::bap::activity_message::sensor_auth_update
