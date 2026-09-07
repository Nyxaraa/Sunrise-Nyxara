#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../../../middleware/content/packages/reader/reader.h"
#include "activity_sdk_activity_inventory.h"
#include "activity_sdk_topology_inventory.h"

namespace sunrise::client::content::activity::sdk_generation::activity_enrichment_inventory {

/** One dynamically joined activity definition and exact localized display name. */
struct Row final {
    std::uint32_t activityIndex{};
    std::uint32_t definitionHash{};
    std::uint32_t recordLength{};
    std::int32_t requiredLevel{};
    std::int32_t requiredPower{};
    std::int32_t requiredLevel2{};
    std::int32_t requiredPower2{};
    std::uint8_t typeIndex{};
    topology_inventory::Text displayName{};
    bool authoredEmptyName{};
};

/** Complete package-derived enrichment retained until final string-table linking. */
struct Snapshot final {
    std::vector<Row> rows{};
    std::uint32_t resolvedNameCount{};
    std::uint32_t authoredEmptyNameCount{};
};

/** Validates identity order, UTF-8, and explicit exact-versus-authored-empty accounting. */
[[nodiscard]] bool validate(const Snapshot& snapshot) noexcept;

/**
 * Builds the all-activity enrichment from installed activity and localized-string packages.
 * @param source Installed package source with borrowed keys.
 * @param scratch Caller-owned package-reader caches.
 * @param activities Complete executable activity-definition inventory.
 * @param output Receives only a complete dynamic join.
 */
[[nodiscard]] bool build(const middleware::content::packages::reader::Source& source,
                         middleware::content::packages::reader::Scratch& scratch,
                         const activity_inventory::Snapshot& activities,
                         Snapshot& output) noexcept;

/** Requires every native activity record to reproduce the enrichment identity fields. */
[[nodiscard]] bool validate_source(const activity_inventory::Snapshot& source,
                                   const Snapshot& enrichment) noexcept;

/** Adds display names and the exact content-join flag to an otherwise closed native topology. */
[[nodiscard]] bool apply(const Snapshot& enrichment,
                         topology_inventory::Snapshot& topology) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::activity_enrichment_inventory
