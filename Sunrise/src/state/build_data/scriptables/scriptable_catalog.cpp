#include "scriptable_catalog.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>

namespace sunrise::state::build_data::scriptables {
namespace {

const std::shared_ptr<const Snapshot> g_empty = std::make_shared<const Snapshot>();
std::atomic<std::shared_ptr<const Snapshot>> g_snapshot{g_empty};

} // namespace

void publish(std::shared_ptr<Snapshot> value) noexcept {
    if (value == nullptr) {
        return;
    }
    g_snapshot.store(std::shared_ptr<const Snapshot>(std::move(value)), std::memory_order_release);
}

SnapshotView snapshot() noexcept {
    return g_snapshot.load(std::memory_order_acquire);
}

/** Retains the exact bounded container-placement graph from the current catalog. */
ContainerPlacementView container_placement_view() noexcept {
    ContainerPlacementView view{};
    view.catalog = snapshot();
    if (view.catalog == nullptr || view.catalog->coverage != BuildCoverage::full) {
        view.catalog.reset();
        return view;
    }
    view.lists = view.catalog->containerPlacementLists;
    view.owners = view.catalog->containerPlacementOwners;
    view.placements = view.catalog->containerPlacements;
    view.configs = view.catalog->containerPlacementConfigs;
    view.components = view.catalog->containerPlacementComponents;
    view.diagnostics = view.catalog->containerPlacementDiagnostics;
    return view;
}

/** Returns one validated adjacency range or an empty span. */
template <typename Value>
[[nodiscard]] std::span<const Value>
validated_range(std::span<const Value> values, std::uint32_t first, std::uint32_t count) noexcept {
    const std::size_t begin = first;
    const std::size_t length = count;
    return begin <= values.size() && length <= values.size() - begin ? values.subspan(begin, length)
                                                                     : std::span<const Value>{};
}

/** @return True when one row address is an exact element of the supplied immutable view. */
template <typename Value>
[[nodiscard]] bool
row_index(std::span<const Value> values, const Value& row, std::size_t& output) noexcept {
    output = 0;
    if (values.empty()) {
        return false;
    }
    const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(values.data());
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&row);
    if (address < first) {
        return false;
    }
    const std::uintptr_t byteOffset = address - first;
    if (byteOffset >= values.size_bytes() || byteOffset % sizeof(Value) != 0) {
        return false;
    }
    output = static_cast<std::size_t>(byteOffset / sizeof(Value));
    return &values[output] == &row;
}

/** @return True when one row address is owned by the supplied immutable view. */
template <typename Value>
[[nodiscard]] bool owns_row(std::span<const Value> values, const Value& row) noexcept {
    std::size_t ignored = 0;
    return row_index(values, row, ignored);
}

/** Returns one bounded child range only when every row points back to the exact parent row. */
template <typename Value, typename Predicate>
[[nodiscard]] std::span<const Value> validated_owned_range(std::span<const Value> values,
                                                           std::uint32_t first,
                                                           std::uint32_t count,
                                                           Predicate ownsRow) noexcept {
    const std::span<const Value> range = validated_range(values, first, count);
    return range.size() == count && std::all_of(range.begin(), range.end(), ownsRow)
               ? range
               : std::span<const Value>{};
}

/** Returns configs only for an exact placement row and exact child back-references. */
std::span<const ContainerPlacementConfig>
container_placement_configs(const ContainerPlacementView& view,
                            const ContainerPlacement& placement) noexcept {
    std::size_t placementRow = 0;
    if (!row_index(view.placements, placement, placementRow)) {
        return {};
    }
    return validated_owned_range(view.configs,
                                 placement.firstConfig,
                                 placement.configCount,
                                 [placementRow](const ContainerPlacementConfig& config) noexcept {
                                     return config.placementRow == placementRow;
                                 });
}

/** Returns components only for an exact config row and exact child back-references. */
std::span<const ContainerPlacementComponent>
container_placement_components(const ContainerPlacementView& view,
                               const ContainerPlacementConfig& config) noexcept {
    std::size_t configRow = 0;
    if (!row_index(view.configs, config, configRow)) {
        return {};
    }
    return validated_owned_range(
        view.components,
        config.firstComponent,
        config.componentCount,
        [configRow](const ContainerPlacementComponent& component) noexcept {
            return component.configRow == configRow;
        });
}

bool container_placement_owner_applies(const ContainerPlacementOwner& owner,
                                       std::uint32_t bubbleRow) noexcept {
    return owner.context == SpatialContextJoin::packageStemBubble
           && bubbleRow < std::numeric_limits<std::uint64_t>::digits
           && (owner.scenarioBubbleMask & (std::uint64_t{1} << bubbleRow)) != 0;
}

/** Retains the bounded exact type-23 placement-identifier graph. */
Type23PlacementView type23_placement_view() noexcept {
    Type23PlacementView view{};
    view.catalog = snapshot();
    if (view.catalog == nullptr || view.catalog->coverage != BuildCoverage::full) {
        view.catalog.reset();
        return view;
    }
    view.links = view.catalog->type23PlacementLinks;
    view.candidates = view.catalog->type23PlacementCandidates;
    view.diagnostics = view.catalog->type23PlacementDiagnostics;
    return view;
}

/** Returns type-23 candidates only for an exact complete link and exact child back-references. */
std::span<const Type23PlacementCandidate>
type23_placement_candidates(const Type23PlacementView& view,
                            const Type23PlacementLink& link) noexcept {
    std::size_t linkRow = 0;
    if (!link.complete || !row_index(view.links, link, linkRow)) {
        return {};
    }
    return validated_owned_range(view.candidates,
                                 link.firstCandidate,
                                 link.candidateCount,
                                 [linkRow](const Type23PlacementCandidate& candidate) noexcept {
                                     return candidate.linkRow == linkRow;
                                 });
}

/** Retains the bounded exact type-4 descriptor-to-placement-candidate graph. */
EmbeddedPlacementView embedded_placement_view() noexcept {
    EmbeddedPlacementView view{};
    view.catalog = snapshot();
    if (view.catalog == nullptr || view.catalog->coverage != BuildCoverage::full) {
        view.catalog.reset();
        return view;
    }
    view.links = view.catalog->embeddedPlacementLinks;
    view.placements = view.catalog->embeddedPlacements;
    view.diagnostics = view.catalog->embeddedPlacementDiagnostics;
    return view;
}

/** Returns type-4 candidates only for an exact link row and exact child back-references. */
std::span<const EmbeddedPlacement>
embedded_placement_candidates(const EmbeddedPlacementView& view,
                              const EmbeddedPlacementLink& link) noexcept {
    std::size_t linkRow = 0;
    if (!row_index(view.links, link, linkRow)) {
        return {};
    }
    return validated_owned_range(view.placements,
                                 link.firstCandidate,
                                 link.candidateCount,
                                 [linkRow](const EmbeddedPlacement& placement) noexcept {
                                     return placement.linkRow == linkRow;
                                 });
}

/** Retains and bounds every process-only static spatial candidate array. */
StaticSpatialView static_spatial_view() noexcept {
    StaticSpatialView view{};
    view.catalog = snapshot();
    if (view.catalog == nullptr || view.catalog->coverage != BuildCoverage::full) {
        view.catalog.reset();
        return view;
    }
    view.tables = view.catalog->staticSpatialTables;
    view.owners = view.catalog->staticSpatialOwners;
    view.instances = view.catalog->staticSpatialInstances;
    view.unresolvedReads = view.catalog->staticSpatialUnresolvedReads;
    view.dropped = view.catalog->staticSpatialDropped;
    view.contextResolved = view.catalog->staticSpatialContextResolved;
    view.complete = view.catalog->staticSpatialComplete;
    return view;
}

/** Returns static instances only for an exact complete table and exact child back-references. */
std::span<const StaticSpatialInstance>
static_spatial_instances(const StaticSpatialView& view, const StaticSpatialTable& table) noexcept {
    std::size_t tableRow = 0;
    if (!table.complete || !row_index(view.tables, table, tableRow)) {
        return {};
    }
    return validated_owned_range(view.instances,
                                 table.firstInstance,
                                 table.instanceCount,
                                 [tableRow](const StaticSpatialInstance& instance) noexcept {
                                     return instance.tableRow == tableRow;
                                 });
}

bool static_spatial_owner_applies(const StaticSpatialOwner& owner,
                                  std::uint32_t bubbleRow) noexcept {
    return owner.context == SpatialContextJoin::packageStemBubble
           && bubbleRow < std::numeric_limits<std::uint64_t>::digits
           && (owner.scenarioBubbleMask & (std::uint64_t{1} << bubbleRow)) != 0;
}

/** Retains the bounded exact slot-to-trigger-volume graph from the current catalog. */
TriggerVolumeView trigger_volume_view() noexcept {
    TriggerVolumeView view{};
    view.catalog = snapshot();
    if (view.catalog == nullptr || view.catalog->coverage != BuildCoverage::full) {
        view.catalog.reset();
        return view;
    }
    view.tables = view.catalog->triggerVolumeTables;
    view.owners = view.catalog->triggerVolumeOwners;
    view.incomingReferences = view.catalog->triggerVolumeIncomingReferences;
    view.instances = view.catalog->triggerVolumeInstances;
    view.vertices = view.catalog->triggerVolumeVertices;
    view.triangles = view.catalog->triggerVolumeTriangles;
    view.diagnostics = view.catalog->triggerVolumeDiagnostics;
    return view;
}

/** Returns trigger instances only for an exact complete table and exact child back-references. */
std::span<const TriggerVolumeInstance>
trigger_volume_instances(const TriggerVolumeView& view, const TriggerVolumeTable& table) noexcept {
    std::size_t tableRow = 0;
    if (!table.complete || !row_index(view.tables, table, tableRow)) {
        return {};
    }
    return validated_owned_range(view.instances,
                                 table.firstInstance,
                                 table.instanceCount,
                                 [tableRow](const TriggerVolumeInstance& instance) noexcept {
                                     return instance.tableRow == tableRow;
                                 });
}

/** Returns incoming references only for an exact owner row and exact child back-references. */
std::span<const TriggerVolumeIncomingReference>
trigger_volume_incoming_references(const TriggerVolumeView& view,
                                   const TriggerVolumeOwner& owner) noexcept {
    std::size_t ownerRow = 0;
    if (!row_index(view.owners, owner, ownerRow)) {
        return {};
    }
    return validated_owned_range(
        view.incomingReferences,
        owner.firstIncomingReference,
        owner.incomingReferenceCount,
        [ownerRow](const TriggerVolumeIncomingReference& reference) noexcept {
            return reference.ownerRow == ownerRow;
        });
}

std::span<const TriggerVolumeVertex>
trigger_volume_vertices(const TriggerVolumeView& view,
                        const TriggerVolumeInstance& instance) noexcept {
    if (!instance.complete || !owns_row(view.instances, instance)) {
        return {};
    }
    return validated_range(view.vertices, instance.firstVertex, instance.vertexCount);
}

std::span<const TriggerVolumeTriangle>
trigger_volume_triangles(const TriggerVolumeView& view,
                         const TriggerVolumeInstance& instance) noexcept {
    if (!instance.complete || !owns_row(view.instances, instance)) {
        return {};
    }
    return validated_range(view.triangles, instance.firstTriangle, instance.triangleCount);
}

void clear() noexcept {
    g_snapshot.store(g_empty, std::memory_order_release);
}

} // namespace sunrise::state::build_data::scriptables
