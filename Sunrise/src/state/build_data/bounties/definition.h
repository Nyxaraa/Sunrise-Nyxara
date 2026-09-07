#pragma once

#include <cstddef>
#include <cstdint>

namespace sunrise::state::build_data::bounties {

/** Bucket 40 tier 1 rows in the shipped build. It declares 684. */
inline constexpr std::size_t kDefinitionCapacity = 1024;

/** Rows one pool may hold. The widest shipped pool is Eva's Dawning, at 22. */
inline constexpr std::size_t kPoolCapacity = 64;

/** Inventory bucket every repeatable bounty lives in. */
inline constexpr std::uint8_t kBountyBucketId = 40;

/** Rarity ladder byte every repeatable bounty carries. */
inline constexpr std::uint8_t kBountyTier = 1;

/**
 * Localized item-type name of one bounty, as the pair that names it.
 * Nothing resolves the text; the pair alone is what groups a pool, so it is compared raw.
 */
struct ItemType {
    std::uint32_t bank{};
    std::uint32_t hash{};

    /** @return True when both halves of the pair match. */
    [[nodiscard]] constexpr bool operator==(const ItemType& other) const noexcept = default;
};

/** One bucket 40 tier 1 item and the item-type its pool is keyed by. */
struct Definition {
    ItemType itemType{};
    std::uint16_t itemIndex{};
};

} // namespace sunrise::state::build_data::bounties
