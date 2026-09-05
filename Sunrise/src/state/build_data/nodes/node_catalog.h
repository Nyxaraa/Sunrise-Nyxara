#pragma once

#include <cstddef>
#include <span>

#include "definition.h"

namespace sunrise::state::build_data::nodes {

/** Clears all generated nodes. */
void clear() noexcept;

/** Checks capacity, native row order, and child bounds. */
[[nodiscard]] bool valid(std::span<const Definition> definitions) noexcept;

/** Replaces all nodes atomically after validation. */
[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;

/** Copies all nodes in native order. */
[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

/** @return Number of generated node definitions, read under the lock. */
[[nodiscard]] std::size_t count() noexcept;

/** Sets account-scoped lore visibility flags. */
std::size_t apply_visibility(std::span<std::uint8_t> accountFlags) noexcept;

/**
 * Calls back for every node, under the shared lock.
 *
 * The callback runs under the catalog's shared lock and must not re-enter it.
 * @param context Passed through untouched.
 * @param visit Called once per node in native order.
 */
void for_each(void* context, void (*visit)(void*, const Definition&) noexcept) noexcept;

/** Sets character-scoped lore visibility flags. */
std::size_t apply_character_visibility(std::span<std::byte> characterFlags) noexcept;

/**
 * Opens value-gated lore categories. Call after progress writers because some gates share bars.
 */
std::size_t apply_category_gates(std::span<std::int32_t> objectiveValues) noexcept;

} // namespace sunrise::state::build_data::nodes
