#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"

namespace sunrise::state::build_data::bounties {

/** Clears every extracted bounty row. */
void clear() noexcept;

/**
 * Checks one complete bounty table.
 * @param definitions Candidate rows in ascending item-index order.
 * @return True when the rows fit storage, name an item type, and stay in order.
 */
[[nodiscard]] bool valid(std::span<const Definition> definitions) noexcept;

/**
 * Replaces the extracted bounty table in one step.
 * @param definitions Complete rows in ascending item-index order.
 * @return True when the rows pass the checks and fit fixed State storage.
 */
[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;

/**
 * Lists every item sharing one item-type, which is the pool a repeatable vendor rolls from.
 * @param itemType Item-type pair the pool is keyed by.
 * @param output Caller-owned fixed item-index storage.
 * @param count Receives the pool size, or zero when output is too small.
 * @return True when the pool is nonempty and fits output.
 */
[[nodiscard]] bool
pool(const ItemType& itemType, std::span<std::uint16_t> output, std::size_t& count) noexcept;

/**
 * Copies every row in ascending item-index order.
 * @param output Caller-owned fixed row storage.
 * @param count Receives the copied row count, or zero when output is too small.
 * @return True when output can hold every row.
 */
[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

/** @return The bounty row count, read under the lock. */
[[nodiscard]] std::size_t count() noexcept;

} // namespace sunrise::state::build_data::bounties
