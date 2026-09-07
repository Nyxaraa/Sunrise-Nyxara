#include "sobject_catalog.h"

#include <shared_mutex>

#include "../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::sobjects {
namespace {

core::threading::SrwLock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;

} // namespace

/** Clears the table while no reader can observe a partial replacement. */
void clear() noexcept {
    const std::lock_guard guard(g_lock);
    g_definitions.clear();
}

/**
 * Checks the rows are dense and fit.
 * A row's position is its identity, so shape is the only thing this table can assert.
 */
bool valid(std::span<const Definition> definitions) noexcept {
    return !definitions.empty() && definitions.size() <= kDefinitionCapacity;
}

/** Replaces the whole table in one step. */
bool replace(std::span<const Definition> definitions) noexcept {
    if (!valid(definitions)) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    return g_definitions.replace(definitions);
}

/** Copies every row in incident-target order. */
bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.snapshot(output, count);
}

/** Finds one row by the target index an incident carries. */
bool find(std::uint16_t targetIndex, Definition& definition) noexcept {
    const std::shared_lock guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    if (targetIndex >= rows.size()) {
        return false;
    }
    definition = rows[targetIndex];
    return true;
}

/** @return Number of installed rows, read under the lock. */
std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.count();
}

} // namespace sunrise::state::build_data::sobjects
