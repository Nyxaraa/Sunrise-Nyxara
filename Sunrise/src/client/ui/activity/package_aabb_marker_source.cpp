#include "package_aabb_marker_source.h"

#include <cstddef>

#include "../../../middleware/content/packages/tables/static_spatial_aabb_transform.h"
#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "authored_placement_marker.h"

namespace sunrise::client::ui::activity::package_aabb_marker_source {
namespace {

namespace catalog = state::build_data::scriptables;
namespace marker = authored_placement_marker;
namespace spatial_aabb = middleware::content::packages::tables::static_spatial_aabb;

/** @return True when a tag-name row has one selected package-name candidate. */
[[nodiscard]] bool selected_name(const catalog::Snapshot& source, std::uint32_t row) noexcept {
    return row < source.tagNames.size()
           && source.tagNames[row].selectedCandidate < source.nameCandidates.size();
}

} // namespace

/** Builds one exact world AABB anchor from its owner and instance row identity. */
bool build(const catalog::Snapshot& source,
           std::uint32_t ownerRow,
           std::uint32_t instanceRow,
           marker::Anchor& output) noexcept {
    output = {};
    if (ownerRow >= source.staticSpatialOwners.size()
        || instanceRow >= source.staticSpatialInstances.size()) {
        return false;
    }
    const auto& owner = source.staticSpatialOwners[ownerRow];
    if (owner.tableRow >= source.staticSpatialTables.size()
        || owner.placementRow >= source.containerPlacements.size()) {
        return false;
    }
    const auto& table = source.staticSpatialTables[owner.tableRow];
    const auto& instance = source.staticSpatialInstances[instanceRow];
    const auto& placement = source.containerPlacements[owner.placementRow];
    const std::size_t first = table.firstInstance;
    const std::size_t count = table.instanceCount;
    if (!table.complete || instanceRow < first || instanceRow - first >= count
        || instance.tableRow != owner.tableRow || instance.instanceIndex != instanceRow - first
        || placement.objectListTag != owner.objectListTag
        || placement.entryIndex != owner.objectListEntry) {
        return false;
    }

    const spatial_aabb::Vector4 translationScale{placement.position[0],
                                                 placement.position[1],
                                                 placement.position[2],
                                                 placement.uniformScale};
    const spatial_aabb::Bounds local{instance.localMinimum, instance.localMaximum};
    spatial_aabb::Bounds world{};
    if (!spatial_aabb::world_bounds(placement.rotation, translationScale, local, world)) {
        return false;
    }

    output.sourceKind = marker::AnchorSource::packageAabb;
    output.sourceRow = instanceRow;
    output.ownerRow = ownerRow;
    output.objectListTag = owner.objectListTag;
    output.classListTag = placement.classListTag;
    output.entryIndex = owner.objectListEntry;
    output.tableRow = owner.tableRow;
    output.resourceTag = instance.resourceTag;
    output.scenarioBubbleMask = owner.scenarioBubbleMask;
    for (std::size_t lane = 0; lane < output.position.size(); ++lane) {
        output.boundsMinimum[lane] = world.minimum[lane];
        output.boundsMaximum[lane] = world.maximum[lane];
        const double centre =
            (static_cast<double>(world.minimum[lane]) + static_cast<double>(world.maximum[lane]))
            * 0.5;
        output.position[lane] = static_cast<float>(centre);
    }
    return output.objectListTag != 0;
}

/** @return True while one retained owner-and-instance identity remains current. */
bool current(const catalog::Snapshot& source, const marker::Anchor& anchor) noexcept {
    if (anchor.sourceKind != marker::AnchorSource::packageAabb
        || anchor.ownerRow >= source.staticSpatialOwners.size()
        || anchor.sourceRow >= source.staticSpatialInstances.size()
        || anchor.tableRow >= source.staticSpatialTables.size()) {
        return false;
    }
    const auto& owner = source.staticSpatialOwners[anchor.ownerRow];
    const auto& instance = source.staticSpatialInstances[anchor.sourceRow];
    const auto& table = source.staticSpatialTables[anchor.tableRow];
    if (owner.placementRow >= source.containerPlacements.size()) {
        return false;
    }
    const auto& placement = source.containerPlacements[owner.placementRow];
    const std::size_t first = table.firstInstance;
    const std::size_t count = table.instanceCount;
    return table.complete && anchor.sourceRow >= first && anchor.sourceRow - first < count
           && owner.tableRow == anchor.tableRow && instance.tableRow == anchor.tableRow
           && instance.instanceIndex == anchor.sourceRow - first
           && instance.resourceTag == anchor.resourceTag
           && owner.scenarioBubbleMask == anchor.scenarioBubbleMask
           && placement.objectListTag == anchor.objectListTag
           && placement.entryIndex == anchor.entryIndex
           && placement.classListTag == anchor.classListTag;
}

/** @return The strongest allowed table, resource, or package name row for one AABB. */
std::uint32_t name_row(const catalog::Snapshot& source, const marker::Anchor& anchor) noexcept {
    if (anchor.sourceRow >= source.staticSpatialInstances.size()
        || anchor.tableRow >= source.staticSpatialTables.size()
        || anchor.ownerRow >= source.staticSpatialOwners.size()) {
        return catalog::kNoRow;
    }
    const auto& instance = source.staticSpatialInstances[anchor.sourceRow];
    if (selected_name(source, instance.resourceNameRow)) {
        return instance.resourceNameRow;
    }
    const auto& table = source.staticSpatialTables[anchor.tableRow];
    if (selected_name(source, table.tableNameRow)) {
        return table.tableNameRow;
    }
    if (selected_name(source, table.boundsNameRow)) {
        return table.boundsNameRow;
    }
    const auto& owner = source.staticSpatialOwners[anchor.ownerRow];
    if (selected_name(source, owner.objectListNameRow)) {
        return owner.objectListNameRow;
    }
    if (selected_name(source, owner.parentNameRow)) {
        return owner.parentNameRow;
    }
    return selected_name(source, owner.containerNameRow) ? owner.containerNameRow : catalog::kNoRow;
}

} // namespace sunrise::client::ui::activity::package_aabb_marker_source
