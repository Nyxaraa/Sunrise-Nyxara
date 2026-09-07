#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::state::build_data::collectibles {

/** The Collections protocol carries a present bit followed by a 15-bit native row index. */
inline constexpr std::size_t kDefinitionCapacity = 1U << 15U;
/** Some collectible rows deliberately do not resolve to an inventory item. */
inline constexpr std::uint16_t kUnavailableItemDefinitionIndex = 0xFFFFU;

/**
 * An acquisition carrying this index has no collectible and skips every collectible step.
 * Prepare and commit must both carry it, or the acquisition's consistency guard refuses.
 */
inline constexpr std::uint16_t kNoCollectibleIndex = 0xFFFEU;
/** A collectible with no acquisition charge carries this native requirement-set sentinel. */
inline constexpr std::uint16_t kUnavailableMaterialRequirementSetIndex = 0xFFFFU;

/** A collectible whose acquired state is not one plain flag test carries this instead of a slot. */
inline constexpr std::uint16_t kUnavailableFlagSlot = 0xFFFFU;

/** A collectible whose acquired flag no mapping table addresses carries this instead of a row. */
inline constexpr std::uint16_t kUnavailableFlagIndex = 0xFFFFU;

/** Installed requirement sets contain at most six material rows. */
inline constexpr std::size_t kMaterialRequirementCapacity = 6;

/** One native material row attached to a Collections acquisition. */
struct MaterialRequirement {
    std::uint32_t quantity{};
    std::uint16_t itemDefinitionIndex{kUnavailableItemDefinitionIndex};
    bool deleteOnAction{};
    bool omitFromRequirements{};
};

/** One installed-build collectible row and the item-definition row it grants. */
struct Definition {
    std::uint32_t collectibleHash{};
    std::uint32_t materialRequirementSetHash{};
    std::uint16_t collectibleIndex{};
    std::uint16_t itemDefinitionIndex{kUnavailableItemDefinitionIndex};
    std::uint16_t materialRequirementSetIndex{kUnavailableMaterialRequirementSetIndex};
    /** Unlock flag slot the acquired-state expression tests, when it tests exactly one. */
    std::uint16_t acquiredFlagSlot{kUnavailableFlagSlot};
    /** Bank row that slot feeds inside the object its kind names, or the unavailable row. */
    std::uint16_t acquiredFlagIndex{kUnavailableFlagIndex};
    std::uint8_t materialRequirementCount{};
    std::array<MaterialRequirement, kMaterialRequirementCapacity> materialRequirements{};
};

/** Clears every generated collectible mapping. */
void clear() noexcept;

/** @return True when every native collectible index appears exactly once. */
[[nodiscard]] bool valid(std::span<const Definition> definitions) noexcept;

/** Replaces the complete dense collectible table in one publication. */
[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;

/** Finds one collectible by the native 15-bit index carried by the request. */
[[nodiscard]] bool find(std::uint16_t collectibleIndex, Definition& definition) noexcept;

/**
 * Answers whether any collectible grants one installed item row.
 * @param itemDefinitionIndex Installed item-definition row.
 * @return True when Collections can grant that item, so an account can come to own it.
 */
[[nodiscard]] bool grants_item(std::uint16_t itemDefinitionIndex) noexcept;

/**
 * Finds the collectible that grants one installed item row, the reverse of `find`.
 * @param itemDefinitionIndex Installed item-definition row.
 * @param collectibleIndex Receives the first collectible naming that item, in native index order.
 *        Left untouched when none does, so a caller's sentinel survives.
 * @return True when a collectible grants that item.
 */
[[nodiscard]] bool find_granting(std::uint16_t itemDefinitionIndex,
                                 std::uint16_t& collectibleIndex) noexcept;

/** Copies every row in native collectible-index order. */
[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

/** @return Number of installed-build collectible mappings. */
[[nodiscard]] std::size_t count() noexcept;

} // namespace sunrise::state::build_data::collectibles
