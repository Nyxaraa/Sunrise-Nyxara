#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"

namespace sunrise::state::build_data::records::rewards {

/** Checks fixed capacity; an absent reward table is valid empty input. */
[[nodiscard]] bool valid(std::span<const RewardRow> rows) noexcept;

/** Replaces the generated reward table atomically after validation. */
[[nodiscard]] bool replace(std::span<const RewardRow> rows) noexcept;

/**
 * Visits matching rows under the shared lock; returning false stops the walk.
 */
void visit_for_record(std::uint32_t recordHash, RowVisitor visitor, void* context) noexcept;

} // namespace sunrise::state::build_data::records::rewards
