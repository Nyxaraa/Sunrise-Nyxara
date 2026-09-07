#include "bounty_catalog.h"

#include <shared_mutex>

#include "../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::bounties {
namespace {

core::threading::SrwLock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;

} // namespace

/** Clears every extracted bounty row under the catalog lock. */
void clear() noexcept {
    const std::lock_guard guard(g_lock);
    g_definitions.clear();
}

/** Checks one complete bounty table. */
bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.size() > kDefinitionCapacity) {
        return false;
    }
    for (std::size_t row = 0; row < definitions.size(); ++row) {
        const Definition& definition = definitions[row];
        // An unnamed row would collapse every unnamed bounty into one pool.
        if (definition.itemType.hash == 0
            || (row != 0 && definitions[row - 1].itemIndex >= definition.itemIndex)) {
            return false;
        }
    }
    return true;
}

/** Replaces the extracted bounty table in one step. */
bool replace(std::span<const Definition> definitions) noexcept {
    if (!valid(definitions)) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    return g_definitions.replace(definitions);
}

/** Lists every item sharing one item-type. */
bool pool(const ItemType& itemType, std::span<std::uint16_t> output, std::size_t& count) noexcept {
    count = 0;
    const std::shared_lock guard(g_lock);
    std::size_t used = 0;
    for (const Definition& definition : g_definitions.rows()) {
        if (definition.itemType != itemType) {
            continue;
        }
        // A pool truncated in silence would roll from a subset of what the vendor offers.
        if (used >= output.size()) {
            return false;
        }
        output[used++] = definition.itemIndex;
    }
    count = used;
    return used != 0;
}

/** Copies every row in ascending item-index order. */
bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.snapshot(output, count);
}

/** @return The bounty row count, read under the lock. */
std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.count();
}

} // namespace sunrise::state::build_data::bounties
