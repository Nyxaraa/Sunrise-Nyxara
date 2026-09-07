#pragma once

#include <cstddef>
#include <span>

#include "definition.h"

namespace sunrise::state::build_data::records {

/** Clears every generated record definition and all three flat banks. */
void clear() noexcept;

/**
 * Checks one complete record catalog in native record order.
 * All four arrays are checked together. A record names its rows by range, so a bank short by one
 * row silently repoints every record above it.
 * @param definitions Candidate rows.
 * @param objectives Candidate flat objective bank.
 * @param intervals Candidate flat interval bank.
 * @param rewards Candidate flat reward bank.
 * @return True when index n sits at position n and every range ends inside its bank.
 */
[[nodiscard]] bool valid(std::span<const Definition> definitions,
                         std::span<const Objective> objectives,
                         std::span<const Interval> intervals,
                         std::span<const Reward> rewards) noexcept;

/**
 * Replaces the generated record catalog in one step.
 * @param definitions Complete dense rows in native index order.
 * @param objectives Complete flat bank in record then row order.
 * @param intervals Complete flat bank in record then step order.
 * @param rewards Complete flat bank in record then row order.
 * @return True when the rows pass the checks and fit fixed State storage.
 */
[[nodiscard]] bool replace(std::span<const Definition> definitions,
                           std::span<const Objective> objectives,
                           std::span<const Interval> intervals,
                           std::span<const Reward> rewards) noexcept;

/**
 * Finds one record by the native row a claim names.
 * @param definitionIndex Native record row.
 * @param definition Receives the row only on success.
 * @return True when the domain is complete and the row exists.
 */
[[nodiscard]] bool find(std::uint16_t definitionIndex, Definition& definition) noexcept;

/**
 * Reads one objective row of one record.
 * @param definition Owning record, copied earlier from this catalog.
 * @param row Objective ordinal inside that record.
 * @param output Receives the row only on success.
 * @return True when the record owns that row and the bank still holds it.
 */
[[nodiscard]] bool
objective(const Definition& definition, std::size_t row, Objective& output) noexcept;

/**
 * Reads one interval step of one record.
 * @param definition Owning record, copied earlier from this catalog.
 * @param row Step ordinal inside that record.
 * @param output Receives the row only on success.
 * @return True when the record owns that step and the bank still holds it.
 */
[[nodiscard]] bool
interval(const Definition& definition, std::size_t row, Interval& output) noexcept;

/**
 * Copies the reward rows one record grants.
 * @param definition Owning record, copied earlier from this catalog.
 * @param output Caller-owned fixed row storage.
 * @param count Receives the copied row count, or zero when output is too small.
 * @return True when output can hold the whole range.
 */
[[nodiscard]] bool
rewards(const Definition& definition, std::span<Reward> output, std::size_t& count) noexcept;

/**
 * Copies every row in native record order.
 * @param output Caller-owned fixed row storage.
 * @param count Receives the copied row count, or zero when output is too small.
 * @return True when output can hold every row.
 */
[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

/** Copies the whole flat objective bank. @param output Storage. @param count Rows copied. */
[[nodiscard]] bool snapshot_objectives(std::span<Objective> output, std::size_t& count) noexcept;

/** Copies the whole flat interval bank. @param output Storage. @param count Rows copied. */
[[nodiscard]] bool snapshot_intervals(std::span<Interval> output, std::size_t& count) noexcept;

/** Copies the whole flat reward bank. @param output Storage. @param count Rows copied. */
[[nodiscard]] bool snapshot_rewards(std::span<Reward> output, std::size_t& count) noexcept;

/** @return Number of generated record definitions, read under the lock. */
[[nodiscard]] std::size_t count() noexcept;

/** @return The objective bank row count, read under the lock. */
[[nodiscard]] std::size_t objective_count() noexcept;

/** @return The interval bank row count, read under the lock. */
[[nodiscard]] std::size_t interval_count() noexcept;

/** @return The reward bank row count, read under the lock. */
[[nodiscard]] std::size_t reward_count() noexcept;

} // namespace sunrise::state::build_data::records
