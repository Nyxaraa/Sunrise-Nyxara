#pragma once

#include <cstdint>
#include <vector>

#include "activity_sdk_activity_inventory.h"
#include "activity_sdk_policy_inventory.h"
#include "activity_sdk_topology_enrichment.h"
#include "activity_sdk_topology_inventory.h"

namespace sunrise::client::content::activity::sdk_generation::policy_input_adapter {

namespace policy = policy_inventory;

/**
 * Policy inputs whose strings borrow finalized native inventory storage.
 * The activity, topology, and enrichment snapshots must outlive policy build consumption.
 */
struct Snapshot final {
    std::vector<policy::ActivityInput> activities{};
    std::vector<policy::SlotInput> slots{};
    std::vector<policy::SlotAliasInput> slotAliases{};
    std::vector<policy::OccurrenceInput> occurrences{};
    std::uint32_t scenarioCount{};
    std::uint32_t objectCount{};

    /** @return A complete borrowed input view using the compiled host-surface inventory. */
    [[nodiscard]] policy::Inputs view() const noexcept;
};

/**
 * Adapts closed activity/topology rows and validated topology enrichment transactionally.
 * Activity enrichment must already have added final display names and content-exact flags.
 * @param activities The final source of activity join outcomes.
 * @param topology The closed topology after activity enrichment was applied.
 * @param enrichment The validated final slot names, schemas, flags, and local alias ranges.
 * @param output Receives borrowed input rows only after every cross-check succeeds.
 * @return True when row order, final flags, text, and child ranges close exactly.
 */
[[nodiscard]] bool build(const activity_inventory::Snapshot& activities,
                         const topology_inventory::Snapshot& topology,
                         const topology_enrichment::Snapshot& enrichment,
                         Snapshot& output) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::policy_input_adapter
