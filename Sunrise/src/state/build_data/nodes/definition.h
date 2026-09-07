#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::state::build_data::nodes {

/** Fixed capacity above the shipped build's 924 presentation nodes. */
inline constexpr std::size_t kDefinitionCapacity = 1024;

/** Child capacity; the widest shipped lore book owns fifteen records. */
inline constexpr std::size_t kChildCapacity = 64;

/** A node whose expression names no addressable value slot carries this instead of an index. */
inline constexpr std::uint16_t kUnavailableValueIndex = 0xFFFFU;

/** A node whose gate names no addressable flag carries this instead of an index. */
inline constexpr std::uint16_t kUnavailableFlagIndex = 0xFFFFU;

/** Extracted fields needed to publish one node's visibility and progress. */
struct Definition {
    /** Native node row. */
    std::uint16_t definitionIndex{};
    /** Account value mapping row, or unavailable. */
    std::uint16_t valueIndex{kUnavailableValueIndex};
    /** Raw account value slot used to resolve contiguous parent slots. */
    std::int16_t valueSlot{-1};
    /** Raw character value slot, or -1. */
    std::int16_t characterValueSlot{-1};
    /** Account value index of the parent record's chapter bar. */
    std::uint16_t parentValueIndex{kUnavailableValueIndex};
    /** Character value index of the parent record's chapter bar. */
    std::uint16_t parentCharacterValueIndex{kUnavailableValueIndex};
    /** Account flag read by this node's visibility gate. */
    std::uint16_t visibilityFlagIndex{kUnavailableFlagIndex};
    /** Character flag read by this node's visibility gate. */
    std::uint16_t visibilityCharacterFlagIndex{kUnavailableFlagIndex};
    /** Character value index read by this node's progress bar. */
    std::uint16_t characterValueIndex{kUnavailableValueIndex};
    /**
     * True when this node is a lore book, the only kind this build counts.
     * A book is a node owning at least one record that displays a lore row.
     */
    bool loreBook{};
    /** Records this node owns, held at node row `+136`. */
    std::uint8_t childCount{};
    /** Native record rows of the owned records. */
    std::array<std::uint16_t, kChildCapacity> children{};
};

} // namespace sunrise::state::build_data::nodes
