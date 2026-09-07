#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "../../../state/build_data/scriptables/definition.h"

namespace sunrise::client::content::activity::sdk_generation::external_placements {

namespace catalog = state::build_data::scriptables;

/**
 * A placement a scenario loads that the activity object's own lists do not carry.
 * The map package holds most of them, and a type-4 descriptor's embedded array holds the rest.
 */
struct Row final {
    std::uint64_t placedEntryIdentity{};
    /** Zero when the row came from a descriptor-embedded array, which names no object list. */
    std::uint32_t objectListTag{};
    std::uint32_t placementOrdinal{};
    std::uint32_t classListTag{};
    std::uint32_t nameHash{};
    std::array<std::uint32_t, 3> positionBits{};
    std::array<std::uint32_t, 4> quaternionBits{};
    std::uint32_t uniformScaleBits{};
};

/** One scenario's reference to a deduplicated row. */
struct ScenarioRow final {
    std::uint32_t scenarioTag{};
    std::uint32_t rowIndex{};
};

/** Identity and transform decide the row, so one list and its mirror copy share it. */
struct RowKey final {
    std::uint64_t placedEntryIdentity{};
    std::array<std::uint32_t, 3> positionBits{};
    std::array<std::uint32_t, 4> quaternionBits{};
    std::uint32_t uniformScaleBits{};

    [[nodiscard]] bool operator==(const RowKey&) const noexcept = default;
};

struct RowKeyHash final {
    [[nodiscard]] std::size_t operator()(const RowKey& value) const noexcept;
};

/**
 * Deduplicated rows plus the scenarios that carry each one.
 * One destination's rows repeat across all its scenarios, so the rows are stored once and the
 * scenario dimension costs one pair per carrying scenario.
 */
struct Index final {
    std::vector<Row> rows{};
    /** Sorted by scenario tag once `finalize` has run. */
    std::vector<ScenarioRow> scenarioRows{};
    /** Pass-local row lookup, released by `finalize`. */
    std::unordered_map<RowKey, std::uint32_t, RowKeyHash> lookup{};
    /** False when a harvest was dropped, so no consumer may treat this as closed. */
    bool complete{true};
};

/** Harvests one scenario's container and descriptor-embedded placements into the index. */
[[nodiscard]] bool
append(std::uint32_t scenarioTag, const catalog::Snapshot& snapshot, Index& output) noexcept;

/** Sorts the scenario references, drops repeats, and releases the pass-local lookup. */
void finalize(Index& output) noexcept;

/** The sorted references one scenario tag owns. */
[[nodiscard]] std::span<const ScenarioRow> scenario_rows(const Index& index,
                                                         std::uint32_t scenarioTag) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::external_placements
