#include "season_pass_catalog.h"

#include <algorithm>
#include <shared_mutex>

#include "../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::season_pass {
namespace {

// One lock covers both tables. A reward names a wrapper by hash, so a reader must never see one
// table replaced and the other not.
core::threading::SrwLock g_lock;
Table<Reward, kRewardCapacity> g_rewards;
Table<Package, kPackageCapacity> g_packages;

} // namespace

/** Clears the reward rows and the wrapper packages under the catalog lock. */
void clear() noexcept {
    const std::lock_guard guard(g_lock);
    g_rewards.clear();
    g_packages.clear();
}

/** Checks one complete pass catalog. */
bool valid(std::span<const Reward> rewards, std::span<const Package> packages) noexcept {
    if (rewards.empty() || rewards.size() > kRewardCapacity || packages.size() > kPackageCapacity) {
        return false;
    }
    // A row granting nothing would claim its flag and hand the account no item.
    const bool granting = std::all_of(rewards.begin(), rewards.end(), [](const Reward& reward) {
        return reward.itemHash != 0 && reward.quantity != 0;
    });
    return granting && std::all_of(packages.begin(), packages.end(), [](const Package& package) {
               return package.definitionHash != 0 && package.itemCount != 0
                      && package.itemCount <= kPackageItemCapacity;
           });
}

/** Replaces the complete pass catalog in one step. */
bool replace(std::span<const Reward> rewards, std::span<const Package> packages) noexcept {
    if (!valid(rewards, packages)) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    const bool replaced = g_rewards.replace(rewards) && g_packages.replace(packages);
    if (!replaced) {
        g_rewards.clear();
        g_packages.clear();
    }
    return replaced;
}

/** Reads one reward row by the index an opcode-2400 claim names. */
bool find(std::uint16_t rewardIndex, Reward& reward) noexcept {
    reward = {};
    const std::shared_lock guard(g_lock);
    const std::span<const Reward> rows = g_rewards.rows();
    if (rewardIndex >= rows.size()) {
        return false;
    }
    reward = rows[rewardIndex];
    return true;
}

/** Finds the wrapper package one item hash opens into. */
bool find_package(std::uint32_t definitionHash, Package& package) noexcept {
    package = {};
    const std::shared_lock guard(g_lock);
    for (const Package& row : g_packages.rows()) {
        if (row.definitionHash == definitionHash) {
            package = row;
            return true;
        }
    }
    return false;
}

/** Copies every reward row in native reward order. */
bool snapshot(std::span<Reward> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_rewards.snapshot(output, count);
}

/** Copies every wrapper package in declared order. */
bool snapshot_packages(std::span<Package> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_packages.snapshot(output, count);
}

/** @return The reward row count, read under the lock. */
std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_rewards.count();
}

/** @return The wrapper package count, read under the lock. */
std::size_t package_count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_packages.count();
}

} // namespace sunrise::state::build_data::season_pass
