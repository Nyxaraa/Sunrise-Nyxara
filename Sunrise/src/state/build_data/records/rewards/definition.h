#pragma once

#include <cstddef>
#include <cstdint>

namespace sunrise::state::build_data::records::rewards {

/**
 * Most reward rows the shipped table can hold. A handful of Triumphs carry a reward at all, so
 * this is ample headroom rather than a measured ceiling.
 */
inline constexpr std::size_t kRewardCapacity = 4096;
/** Widest reward bundle in the generated table. */
inline constexpr std::size_t kRewardPerRecordCapacity = 3;

/** Installed item row resolved from one generated reward. */
struct ResolvedReward {
    std::uint16_t itemDefinitionIndex{};
    std::int32_t quantity{};
};

/**
 * One manifest-sourced record reward: claiming the named Triumph grants the named item.
 *
 * A record with several authored rewards contributes several rows sharing the same recordHash.
 * This is generated data joined out of Bungie's manifest by an external tool, not extracted from
 * the installed client, so the rows carry authored hashes rather than this build's native indices;
 * `itemHash` is resolved against this build's item table at lookup time, and a hash this build
 * never installed is skipped rather than failing the row.
 */
struct RewardRow {
    /** records::Definition::definitionHash of the Triumph that grants this item. */
    std::uint32_t recordHash{};
    /** Authored DestinyInventoryItemDefinition hash of the granted item. */
    std::uint32_t itemHash{};
    /** Units granted. */
    std::uint32_t quantity{};
};

/** Called once per reward row naming one record, in table order; returning false ends the walk. */
using RowVisitor = bool (*)(void* context, const RewardRow& row) noexcept;

} // namespace sunrise::state::build_data::records::rewards
