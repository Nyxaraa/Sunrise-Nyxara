#pragma once

#include <cstdint>

namespace sunrise::state::lore {

/** Why one exact collectible grant did not change account state. */
enum class GrantOutcome : std::uint8_t {
    granted,
    progressed,
    refused,
    recordNotFound,
    noFlag,
    alreadyHeld,
    notAChapter,
};

/** Grants one exact lore record by its native definition row. */
[[nodiscard]] GrantOutcome grant_record(std::uint16_t definitionIndex) noexcept;

/** Advances one exact counted lore record by one objective unit. */
[[nodiscard]] GrantOutcome advance_record(std::uint16_t definitionIndex) noexcept;

} // namespace sunrise::state::lore
