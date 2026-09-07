#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "definition.h"

namespace sunrise::state::build_data::scriptables {

using SnapshotView = std::shared_ptr<const Snapshot>;

/** Stable spans into one retained immutable spatial-candidate snapshot. */
struct StaticSpatialView final {
    SnapshotView catalog{};
    std::span<const StaticSpatialTable> tables{};
    std::span<const StaticSpatialOwner> owners{};
    std::span<const StaticSpatialInstance> instances{};
    std::uint64_t unresolvedReads{};
    std::uint64_t dropped{};
    bool contextResolved{};
    bool complete{};
};

/** Stable spans into one immutable destination-agnostic container-placement graph. */
struct ContainerPlacementView final {
    SnapshotView catalog{};
    std::span<const ContainerPlacementList> lists{};
    std::span<const ContainerPlacementOwner> owners{};
    std::span<const ContainerPlacement> placements{};
    std::span<const ContainerPlacementConfig> configs{};
    std::span<const ContainerPlacementComponent> components{};
    ContainerPlacementDiagnostics diagnostics{};
};

/** Stable spans into one immutable exact type-23 placement-identifier graph. */
struct Type23PlacementView final {
    SnapshotView catalog{};
    std::span<const Type23PlacementLink> links{};
    std::span<const Type23PlacementCandidate> candidates{};
    Type23PlacementDiagnostics diagnostics{};
};

/** Stable spans into one immutable type-4 descriptor-to-placement-candidate graph. */
struct EmbeddedPlacementView final {
    SnapshotView catalog{};
    std::span<const EmbeddedPlacementLink> links{};
    std::span<const EmbeddedPlacement> placements{};
    EmbeddedPlacementDiagnostics diagnostics{};
};

/** Stable spans into one immutable exact slot-to-trigger-volume graph. */
struct TriggerVolumeView final {
    SnapshotView catalog{};
    std::span<const TriggerVolumeTable> tables{};
    std::span<const TriggerVolumeOwner> owners{};
    std::span<const TriggerVolumeIncomingReference> incomingReferences{};
    std::span<const TriggerVolumeInstance> instances{};
    std::span<const TriggerVolumeVertex> vertices{};
    std::span<const TriggerVolumeTriangle> triangles{};
    TriggerVolumeDiagnostics diagnostics{};
};

/** Publishes one complete immutable view. */
void publish(std::shared_ptr<Snapshot> value) noexcept;

/** Copies ownership of the current immutable view. */
[[nodiscard]] SnapshotView snapshot() noexcept;

/** Retains the exact bounded container-placement graph from the current catalog. */
[[nodiscard]] ContainerPlacementView container_placement_view() noexcept;

/** Returns the validated config range owned by one container placement. */
[[nodiscard]] std::span<const ContainerPlacementConfig>
container_placement_configs(const ContainerPlacementView& view,
                            const ContainerPlacement& placement) noexcept;

/** Returns the validated component range owned by one placement config. */
[[nodiscard]] std::span<const ContainerPlacementComponent>
container_placement_components(const ContainerPlacementView& view,
                               const ContainerPlacementConfig& config) noexcept;

/** Tests one exact container owner against a scenario bubble row. */
[[nodiscard]] bool container_placement_owner_applies(const ContainerPlacementOwner& owner,
                                                     std::uint32_t bubbleRow) noexcept;

/** Retains the bounded exact type-23 placement-identifier graph. */
[[nodiscard]] Type23PlacementView type23_placement_view() noexcept;

/** Returns every retained exact identifier match owned by one type-23 descriptor. */
[[nodiscard]] std::span<const Type23PlacementCandidate>
type23_placement_candidates(const Type23PlacementView& view,
                            const Type23PlacementLink& link) noexcept;

/** Retains the bounded exact type-4 descriptor-to-placement-candidate graph. */
[[nodiscard]] EmbeddedPlacementView embedded_placement_view() noexcept;

/** Returns every retained validated native row owned by one type-4 descriptor link. */
[[nodiscard]] std::span<const EmbeddedPlacement>
embedded_placement_candidates(const EmbeddedPlacementView& view,
                              const EmbeddedPlacementLink& link) noexcept;

/** Retains and bounds every process-only static spatial candidate array. */
[[nodiscard]] StaticSpatialView static_spatial_view() noexcept;

/** Returns only the validated instance range owned by one table row. */
[[nodiscard]] std::span<const StaticSpatialInstance>
static_spatial_instances(const StaticSpatialView& view, const StaticSpatialTable& table) noexcept;

/** Tests one contextual scenario-bubble mask without assigning object identity. */
[[nodiscard]] bool static_spatial_owner_applies(const StaticSpatialOwner& owner,
                                                std::uint32_t bubbleRow) noexcept;

/** Retains the bounded exact slot-to-trigger-volume graph from the current catalog. */
[[nodiscard]] TriggerVolumeView trigger_volume_view() noexcept;

/** Returns the exact-match instance range owned by one trigger-volume table. */
[[nodiscard]] std::span<const TriggerVolumeInstance>
trigger_volume_instances(const TriggerVolumeView& view, const TriggerVolumeTable& table) noexcept;

/** Returns the validated incoming type-31 reference range owned by one trigger occurrence. */
[[nodiscard]] std::span<const TriggerVolumeIncomingReference>
trigger_volume_incoming_references(const TriggerVolumeView& view,
                                   const TriggerVolumeOwner& owner) noexcept;

/** Returns one validated world-vertex range. */
[[nodiscard]] std::span<const TriggerVolumeVertex>
trigger_volume_vertices(const TriggerVolumeView& view,
                        const TriggerVolumeInstance& instance) noexcept;

/** Returns one validated triangle range. */
[[nodiscard]] std::span<const TriggerVolumeTriangle>
trigger_volume_triangles(const TriggerVolumeView& view,
                         const TriggerVolumeInstance& instance) noexcept;

/** Withdraws the transient view. It is never part of build-data persistence. */
void clear() noexcept;

} // namespace sunrise::state::build_data::scriptables
