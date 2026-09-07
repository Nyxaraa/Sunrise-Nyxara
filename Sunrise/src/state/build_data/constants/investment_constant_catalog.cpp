#include "investment_constant_catalog.h"

#include <mutex>
#include <shared_mutex>

#include "../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::constants {
namespace {

// One row, not a table, so it holds the value directly under the shared Lock.
core::threading::SrwLock g_lock;
InvestmentConstants g_constants{};

} // namespace

/** Clears the published investment constants under the catalog lock. */
void clear() noexcept {
    const std::lock_guard guard(g_lock);
    g_constants = {};
}

/** Publishes one extracted constants row. */
bool replace(const InvestmentConstants& value) noexcept {
    if (!valid(value)) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    g_constants = value;
    return true;
}

/** @param value Receives the published constants. @return True when a row is published. */
bool find(InvestmentConstants& value) noexcept {
    const std::shared_lock guard(g_lock);
    value = g_constants;
    return value.extracted;
}

/** @return Copy read under the lock, cleared when nothing is published. */
InvestmentConstants snapshot() noexcept {
    InvestmentConstants value{};
    (void)find(value);
    return value;
}

} // namespace sunrise::state::build_data::constants
