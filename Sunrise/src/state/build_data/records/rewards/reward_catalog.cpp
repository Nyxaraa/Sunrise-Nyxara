#include "reward_catalog.h"

#include <shared_mutex>

#include "../../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::records::rewards {
namespace {

core::threading::SrwLock g_lock;
Table<RewardRow, kRewardCapacity> g_rows;

} // namespace

/** Checks that the rows fit fixed storage. Zero rows is valid. */
bool valid(std::span<const RewardRow> rows) noexcept {
    return rows.size() <= kRewardCapacity;
}

/** Replaces the generated record-reward table in one step. */
bool replace(std::span<const RewardRow> rows) noexcept {
    if (!valid(rows)) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    return g_rows.replace(rows);
}

/** Visits every reward row naming one record, holding the catalog lock for the whole walk. */
void visit_for_record(std::uint32_t recordHash, RowVisitor visitor, void* context) noexcept {
    const std::shared_lock guard(g_lock);
    for (const RewardRow& row : g_rows.rows()) {
        if (row.recordHash != recordHash) {
            continue;
        }
        if (!visitor(context, row)) {
            return;
        }
    }
}

} // namespace sunrise::state::build_data::records::rewards
