#include "record_catalog.h"

#include <algorithm>
#include <shared_mutex>

#include "../../unlocks/unlocks_records.h"
#include "../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::records {
namespace {

// One lock covers all four tables. A record names its rows by range, so a reader must never see
// one table replaced and another not.
core::threading::SrwLock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;
Table<Objective, kObjectiveCapacity> g_objectives;
Table<Interval, kIntervalCapacity> g_intervals;
Table<Reward, kRewardCapacity> g_rewards;

/**
 * Copies one contiguous bank range into caller storage. Call under a shared hold.
 * @tparam Row Bank row type.
 * @param bank Whole published bank.
 * @param offset First row of the range.
 * @param rows Rows in the range.
 * @param output Caller-owned fixed row storage.
 * @param count Receives the copied row count, or zero when the range does not fit.
 * @return True when output can hold the whole range.
 */
template <typename Row>
[[nodiscard]] bool copy_range(std::span<const Row> bank,
                              std::size_t offset,
                              std::size_t rows,
                              std::span<Row> output,
                              std::size_t& count) noexcept {
    count = 0;
    // A definition copied before a replace can name a range the current bank no longer has.
    const bool fits =
        output.size() >= rows && offset <= bank.size() && rows <= bank.size() - offset;
    if (fits) {
        std::copy_n(bank.begin() + static_cast<std::ptrdiff_t>(offset), rows, output.begin());
        count = rows;
    }
    return fits;
}

/**
 * Reads one row of a bank range. Call under a shared hold.
 * @tparam Row Bank row type.
 * @param bank Whole published bank.
 * @param offset First row of the range.
 * @param rows Rows in the range.
 * @param row Ordinal inside the range.
 * @param output Receives the row only on success.
 * @return True when the ordinal is inside the range and the bank still holds it.
 */
template <typename Row>
[[nodiscard]] bool read_row(std::span<const Row> bank,
                            std::size_t offset,
                            std::size_t rows,
                            std::size_t row,
                            Row& output) noexcept {
    if (row >= rows || rows > bank.size() || offset > bank.size() - rows) {
        return false;
    }
    output = bank[offset + row];
    return true;
}

} // namespace

/** Clears every generated record definition and bank under the catalog lock. */
void clear() noexcept {
    // Drop the catalog lock first: the derived publish reads this catalog under the bank lock.
    {
        const std::lock_guard guard(g_lock);
        g_definitions.clear();
        g_objectives.clear();
        g_intervals.clear();
        g_rewards.clear();
    }
    unlocks::records::republish();
}

/** Checks one complete record catalog in native record order. */
bool valid(std::span<const Definition> definitions,
           std::span<const Objective> objectives,
           std::span<const Interval> intervals,
           std::span<const Reward> rewards) noexcept {
    if (definitions.empty() || definitions.size() > kDefinitionCapacity
        || objectives.size() > kObjectiveCapacity || intervals.size() > kIntervalCapacity
        || rewards.size() > kRewardCapacity) {
        return false;
    }
    std::size_t objectiveOffset = 0;
    std::size_t intervalOffset = 0;
    std::size_t rewardOffset = 0;
    std::uint32_t valueIndex = kObjectiveValueIndexBase;
    for (std::size_t row = 0; row < definitions.size(); ++row) {
        const Definition& definition = definitions[row];
        if (definition.definitionIndex != row || definition.objectiveOffset != objectiveOffset
            || definition.intervalOffset != intervalOffset
            || definition.rewardOffset != rewardOffset
            || definition.objectiveValueIndex != valueIndex
            || definition.objectiveCount > objectives.size() - objectiveOffset
            || definition.intervalCount > intervals.size() - intervalOffset
            || definition.rewardCount > rewards.size() - rewardOffset
            || definition.rewardCount > kRewardPerRecordCapacity
            || definition.intervalCount > kIntervalPerRecordCapacity) {
            return false;
        }
        objectiveOffset += definition.objectiveCount;
        intervalOffset += definition.intervalCount;
        rewardOffset += definition.rewardCount;
        // A record naming no objective rows still reserves its run, so the base stays positional.
        valueIndex += definition.objectiveCount != 0 ? definition.objectiveCount
                                                     : kReservedObjectiveValueCount;
    }
    return objectiveOffset == objectives.size() && intervalOffset == intervals.size()
           && rewardOffset == rewards.size();
}

/** Replaces the generated record catalog in one step. */
bool replace(std::span<const Definition> definitions,
             std::span<const Objective> objectives,
             std::span<const Interval> intervals,
             std::span<const Reward> rewards) noexcept {
    if (!valid(definitions, objectives, intervals, rewards)) {
        return false;
    }
    bool replaced = false;
    {
        const std::lock_guard guard(g_lock);
        replaced = g_definitions.replace(definitions) && g_objectives.replace(objectives)
                   && g_intervals.replace(intervals) && g_rewards.replace(rewards);
        if (!replaced) {
            g_definitions.clear();
            g_objectives.clear();
            g_intervals.clear();
            g_rewards.clear();
        }
    }
    if (replaced) {
        unlocks::records::republish();
    }
    return replaced;
}

/** Finds one record by the native row a claim names. */
bool find(std::uint16_t definitionIndex, Definition& definition) noexcept {
    const std::shared_lock guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    if (definitionIndex >= rows.size()) {
        return false;
    }
    definition = rows[definitionIndex];
    return true;
}

/** Reads one objective row of one record. */
bool objective(const Definition& definition, std::size_t row, Objective& output) noexcept {
    const std::shared_lock guard(g_lock);
    return read_row(
        g_objectives.rows(), definition.objectiveOffset, definition.objectiveCount, row, output);
}

/** Reads one interval step of one record. */
bool interval(const Definition& definition, std::size_t row, Interval& output) noexcept {
    const std::shared_lock guard(g_lock);
    return read_row(
        g_intervals.rows(), definition.intervalOffset, definition.intervalCount, row, output);
}

/** Copies the reward rows one record grants. */
bool rewards(const Definition& definition, std::span<Reward> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return copy_range(
        g_rewards.rows(), definition.rewardOffset, definition.rewardCount, output, count);
}

/** Copies every row in native record order. */
bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.snapshot(output, count);
}

/** Copies the whole flat objective bank. */
bool snapshot_objectives(std::span<Objective> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_objectives.snapshot(output, count);
}

/** Copies the whole flat interval bank. */
bool snapshot_intervals(std::span<Interval> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_intervals.snapshot(output, count);
}

/** Copies the whole flat reward bank. */
bool snapshot_rewards(std::span<Reward> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_rewards.snapshot(output, count);
}

/** @return Number of generated record definitions, read under the lock. */
std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.count();
}

/** @return The objective bank row count, read under the lock. */
std::size_t objective_count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_objectives.count();
}

/** @return The interval bank row count, read under the lock. */
std::size_t interval_count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_intervals.count();
}

/** @return The reward bank row count, read under the lock. */
std::size_t reward_count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_rewards.count();
}

} // namespace sunrise::state::build_data::records
