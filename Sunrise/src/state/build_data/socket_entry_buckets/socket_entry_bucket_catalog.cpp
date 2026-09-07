#include "socket_entry_bucket_catalog.h"

#include <mutex>
#include <shared_mutex>

#include "../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::socket_entry_buckets {
namespace {

core::threading::SrwLock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;

} // namespace

/** Clears every resolved entry-bucket row under the catalog lock. */
void clear() noexcept {
    const std::lock_guard guard(g_lock);
    g_definitions.clear();
}

/** Checks that no two rows name the same socket-entry list. */
bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.size() > kDefinitionCapacity) {
        return false;
    }
    for (std::size_t row = 0; row < definitions.size(); ++row) {
        for (std::size_t other = row + 1; other < definitions.size(); ++other) {
            if (definitions[row].socketEntryListIndex == definitions[other].socketEntryListIndex) {
                return false;
            }
        }
    }
    return true;
}

/** Replaces the resolved entry-bucket rows in one step. */
bool replace(std::span<const Definition> definitions) noexcept {
    if (!valid(definitions)) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    return g_definitions.replace(definitions);
}

/** Finds one socket-entry list's resolved per-entry ability-bucket destinations. */
bool find(std::uint16_t socketEntryListIndex, Definition& definition) noexcept {
    definition = {};
    const std::shared_lock guard(g_lock);
    for (const Definition& row : g_definitions.rows()) {
        if (row.socketEntryListIndex == socketEntryListIndex) {
            definition = row;
            return true;
        }
    }
    return false;
}

/** @return Number of resolved entry-bucket rows, read under the lock. */
std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.count();
}

} // namespace sunrise::state::build_data::socket_entry_buckets
