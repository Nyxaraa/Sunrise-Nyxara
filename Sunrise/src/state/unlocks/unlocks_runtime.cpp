#include "unlocks_runtime.h"

#include <limits>
#include <mutex>
#include <shared_mutex>

#include "core/threading/srw_lock.h"

namespace sunrise::state::unlocks {
namespace {

Table g_table{};
core::threading::SrwLock g_lock;

} // namespace

/** Seeds the live unlock banks from the boot policy. */
void publish(const Table& table) noexcept {
    const std::lock_guard guard(g_lock);
    g_table = table;
}

/** @return The live unlock banks. */
const Table& get() noexcept {
    return g_table;
}

/** Restores empty unlock banks. */
void clear() noexcept {
    const std::lock_guard guard(g_lock);
    g_table = Table{};
}

/** Runs one write over the live banks under the exclusive lock. */
void mutate(void* context, void (*apply)(void*, Table&) noexcept) noexcept {
    if (apply == nullptr) {
        return;
    }
    const std::lock_guard guard(g_lock);
    apply(context, g_table);
}

/** @return True when the account acquired flag at this row is set. */
bool account_flag_set(std::uint16_t index) noexcept {
    const std::shared_lock guard(g_lock);
    return index < g_table.accountFlags.size() && g_table.accountFlags[index] == kFlagSet;
}

/** @return True when the selected character's object flag at this row is set. */
bool character_object_flag_set(std::uint16_t index) noexcept {
    const std::shared_lock guard(g_lock);
    return index < g_table.characterObjectFlags.size()
           && g_table.characterObjectFlags[index] == kFlagSet;
}

/** Writes one account acquired flag. */
bool set_account_flag(std::uint16_t index, std::uint8_t value) noexcept {
    const std::lock_guard guard(g_lock);
    if (index >= g_table.accountFlags.size()) {
        return false;
    }
    g_table.accountFlags[index] = value;
    return true;
}

/** @return One account objective value. */
std::int32_t objective_value(std::uint16_t index) noexcept {
    const std::shared_lock guard(g_lock);
    return index < g_table.objectiveValues.size() ? g_table.objectiveValues[index] : 0;
}

/** Writes one account objective value. */
bool set_objective_value(std::uint16_t index, std::int32_t value) noexcept {
    const std::lock_guard guard(g_lock);
    if (index >= g_table.objectiveValues.size()) {
        return false;
    }
    g_table.objectiveValues[index] = value;
    return true;
}

/** Adds to one account objective value. */
bool add_objective_value(std::uint16_t index, std::int32_t amount) noexcept {
    const std::lock_guard guard(g_lock);
    if (index >= g_table.objectiveValues.size()) {
        return false;
    }
    std::int32_t& slot = g_table.objectiveValues[index];
    if (amount > 0 && slot > (std::numeric_limits<std::int32_t>::max)() - amount) {
        return false;
    }
    if (amount < 0 && slot < (std::numeric_limits<std::int32_t>::min)() - amount) {
        return false;
    }
    slot += amount;
    return true;
}

/** Writes one selected-character object flag. */
bool set_character_object_flag(std::uint16_t index, std::uint8_t value) noexcept {
    const std::lock_guard guard(g_lock);
    if (index >= g_table.characterObjectFlags.size()) {
        return false;
    }
    g_table.characterObjectFlags[index] = value;
    return true;
}

/** Writes one selected-character object value. */
bool set_character_object_value(std::uint16_t index, std::int32_t value) noexcept {
    const std::lock_guard guard(g_lock);
    if (index >= g_table.characterObjectValues.size()) {
        return false;
    }
    g_table.characterObjectValues[index] = value;
    return true;
}

/** @return Lane 0 of one account progression. */
std::int32_t account_progression(std::uint16_t definitionIndex) noexcept {
    const std::shared_lock guard(g_lock);
    return definitionIndex < g_table.accountProgressions.size()
               ? g_table.accountProgressions[definitionIndex][0]
               : 0;
}

/** Writes lane 0 of one account progression. */
bool set_account_progression(std::uint16_t definitionIndex, std::int32_t value) noexcept {
    const std::lock_guard guard(g_lock);
    if (definitionIndex >= g_table.accountProgressions.size()) {
        return false;
    }
    g_table.accountProgressions[definitionIndex][0] = value;
    return true;
}

} // namespace sunrise::state::unlocks
