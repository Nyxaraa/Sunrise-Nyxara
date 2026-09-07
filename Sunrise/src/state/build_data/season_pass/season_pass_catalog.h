#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"

namespace sunrise::state::build_data::season_pass {

/** Clears the reward rows and the wrapper packages. */
void clear() noexcept;

/**
 * Checks one complete pass catalog.
 * @param rewards Candidate reward rows in native reward order.
 * @param packages Candidate wrapper packages, which may be empty.
 * @return True when every count and item range holds.
 */
[[nodiscard]] bool valid(std::span<const Reward> rewards,
                         std::span<const Package> packages) noexcept;

/**
 * Replaces the complete pass catalog in one step.
 * @param rewards Complete reward rows in native reward order.
 * @param packages Complete wrapper packages.
 * @return True when the catalog passes validation and fits fixed State storage.
 */
[[nodiscard]] bool replace(std::span<const Reward> rewards,
                           std::span<const Package> packages) noexcept;

/**
 * Reads one reward row by the index an opcode-2400 claim names.
 * @param rewardIndex Native reward-array index.
 * @param reward Receives the row.
 * @return True when the catalog holds that row.
 */
[[nodiscard]] bool find(std::uint16_t rewardIndex, Reward& reward) noexcept;

/**
 * Finds the wrapper package one item hash opens into.
 * @param definitionHash Authored wrapper item hash.
 * @param package Receives the matching wrapper.
 * @return True when a wrapper carries that hash.
 */
[[nodiscard]] bool find_package(std::uint32_t definitionHash, Package& package) noexcept;

/**
 * Copies every reward row in native reward order.
 * @param output Caller-owned fixed row storage.
 * @param count Receives the copied row count, or zero when output is too small.
 * @return True when output can hold every row.
 */
[[nodiscard]] bool snapshot(std::span<Reward> output, std::size_t& count) noexcept;

/**
 * Copies every wrapper package in declared order.
 * @param output Caller-owned fixed row storage.
 * @param count Receives the copied row count, or zero when output is too small.
 * @return True when output can hold every row.
 */
[[nodiscard]] bool snapshot_packages(std::span<Package> output, std::size_t& count) noexcept;

/** @return The reward row count, read under the lock. */
[[nodiscard]] std::size_t count() noexcept;

/** @return The wrapper package count, read under the lock. */
[[nodiscard]] std::size_t package_count() noexcept;

} // namespace sunrise::state::build_data::season_pass
