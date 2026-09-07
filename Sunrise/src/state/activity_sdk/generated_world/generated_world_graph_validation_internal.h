#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "internal.h"

namespace sunrise::state::activity_sdk::generated_world::internal {

// Row and range checks shared by every generated-world graph validator.

/** @return True when one required row names an existing row. */
[[nodiscard]] bool valid_row(std::uint32_t row, std::size_t size) noexcept;

/** @return True when one optional row is absent or names an existing row. */
[[nodiscard]] bool valid_optional_row(std::uint32_t row, std::size_t size) noexcept;

/** @return True when a 32-bit half-open row range stays inside one vector. */
[[nodiscard]] bool valid_range(std::uint32_t first, std::uint32_t count, std::size_t size) noexcept;

/** @return True when one row lies inside a checked half-open owner range. */
[[nodiscard]] bool range_contains(std::uint32_t first,
                                  std::uint32_t count,
                                  std::size_t size,
                                  std::size_t row) noexcept;

/**
 * Validates ordered positive child ranges without scanning any child more than once.
 * Zero-length diagnostic rows may retain their default first-row value and are ignored.
 */
template <typename Parent>
[[nodiscard]] bool canonical_ranges(const std::vector<Parent>& parents,
                                    std::uint32_t Parent::* firstMember,
                                    std::uint32_t Parent::* countMember,
                                    std::size_t childCount) noexcept {
    std::size_t expected = 0;
    for (const Parent& parent : parents) {
        const std::uint32_t first = parent.*firstMember;
        const std::uint32_t count = parent.*countMember;
        if (!valid_range(first, count, childCount)) {
            return false;
        }
        if (count == 0) {
            continue;
        }
        if (static_cast<std::size_t>(first) != expected) {
            return false;
        }
        expected += count;
    }
    return expected == childCount;
}

// Per-family graphs, validated in generated_world_graph_validation_families.cpp.

/** @return True when container placement, config, behavior, and text edges are exact. */
[[nodiscard]] bool
valid_container_graph(const build_data::scriptables::Snapshot& snapshot) noexcept;

/** @return True when every type-23 join row is bounded and internally consistent. */
[[nodiscard]] bool valid_type23_graph(const build_data::scriptables::Snapshot& snapshot) noexcept;

/** @return True when static-spatial and trigger-volume ownership graphs are exact. */
[[nodiscard]] bool valid_spatial_graph(const build_data::scriptables::Snapshot& snapshot) noexcept;

/** @return True when all scenario-owned authored-squad context rows are exact and canonical. */
[[nodiscard]] bool
valid_authored_squad_context_graph(const build_data::scriptables::Snapshot& snapshot) noexcept;

} // namespace sunrise::state::activity_sdk::generated_world::internal
