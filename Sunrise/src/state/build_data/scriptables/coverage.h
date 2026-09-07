#pragma once

#include <cstddef>
#include <cstdint>

#include "definition.h"

namespace sunrise::state::build_data::scriptables {
namespace coverage_internal {

/** Returns the fixed diagnostic row for one structural family. */
[[nodiscard]] inline FamilyCoverageDiagnostic& family(CoverageDiagnostics& diagnostics,
                                                      StructuralFamily value) noexcept {
    return diagnostics.families[static_cast<std::size_t>(value)];
}

/** Adds one or more structural loss causes to a family. */
inline void add_loss(FamilyCoverageDiagnostic& diagnostic, std::uint8_t loss) noexcept {
    diagnostic.status = FamilyCoverageStatus::incomplete;
    diagnostic.lossMask = static_cast<std::uint8_t>(diagnostic.lossMask | loss);
}

/** Copies upstream structural loss because this family depends on that source graph. */
inline void inherit_loss(FamilyCoverageDiagnostic& destination,
                         const FamilyCoverageDiagnostic& source) noexcept {
    if (source.status == FamilyCoverageStatus::incomplete) {
        add_loss(destination, source.lossMask != 0 ? source.lossMask : kFamilyCoverageLossPartial);
    }
}

/** Finishes one lossless family without erasing a prior loss. */
inline void
finish(FamilyCoverageDiagnostic& diagnostic, bool applicable, bool semanticUnresolved) noexcept {
    if (diagnostic.lossMask != 0) {
        diagnostic.status = FamilyCoverageStatus::incomplete;
        return;
    }
    diagnostic.status = !applicable          ? FamilyCoverageStatus::notApplicable
                        : semanticUnresolved ? FamilyCoverageStatus::preservedUnresolved
                                             : FamilyCoverageStatus::complete;
}

/** @return True when any container capacity counter records dropped rows. */
[[nodiscard]] inline bool container_dropped(const ContainerPlacementDiagnostics& value) noexcept {
    return value.droppedLists != 0 || value.droppedOwners != 0 || value.droppedPlacements != 0
           || value.droppedConfigs != 0 || value.droppedComponents != 0;
}

/** @return True when any trigger capacity counter records dropped rows. */
[[nodiscard]] inline bool trigger_dropped(const TriggerVolumeDiagnostics& value) noexcept {
    return value.droppedTables != 0 || value.droppedOwners != 0 || value.droppedInstances != 0
           || value.droppedVertices != 0 || value.droppedTriangles != 0
           || value.droppedIncomingReferences != 0;
}

/** Derives the family ledger only from retained rows and exact pass diagnostics. */
[[nodiscard]] inline CoverageDiagnostics derive(const Snapshot& snapshot) noexcept {
    CoverageDiagnostics output{};
    family(output, StructuralFamily::scenarioTopology).status = FamilyCoverageStatus::complete;

    FamilyCoverageDiagnostic& objects = family(output, StructuralFamily::objectGraph);
    if (snapshot.unresolvedReads != 0) {
        add_loss(objects, kFamilyCoverageLossUnread);
    }
    bool objectSemanticUnresolved = false;
    for (const State& state : snapshot.states) {
        if (!state.resolved) {
            add_loss(objects, kFamilyCoverageLossUnread);
        }
    }
    for (const Object& object : snapshot.objects) {
        if (!object.complete || object.safety == GroupSafety::incomplete) {
            add_loss(objects, kFamilyCoverageLossPartial);
        }
        objectSemanticUnresolved =
            objectSemanticUnresolved || object.safety == GroupSafety::ambiguous;
    }
    const bool objectApplicable =
        !snapshot.objects.empty() || !snapshot.slots.empty() || !snapshot.descriptors.empty();
    finish(objects, objectApplicable, objectSemanticUnresolved);

    FamilyCoverageDiagnostic& references = family(output, StructuralFamily::typedReferences);
    inherit_loss(references, objects);
    bool referenceSemanticUnresolved = false;
    for (const TypedReference& reference : snapshot.references) {
        referenceSemanticUnresolved =
            referenceSemanticUnresolved || reference.join != ReferenceJoin::exact;
    }
    finish(references, !snapshot.references.empty(), referenceSemanticUnresolved);

    FamilyCoverageDiagnostic& authored = family(output, StructuralFamily::authoredPlacements);
    inherit_loss(authored, objects);
    bool authoredSemanticUnresolved = false;
    for (const AuthoredPlacement& placement : snapshot.authoredPlacements) {
        authoredSemanticUnresolved =
            authoredSemanticUnresolved || placement.context == SpatialContextJoin::unresolved;
    }
    finish(authored, !snapshot.authoredPlacements.empty(), authoredSemanticUnresolved);

    const ContainerPlacementDiagnostics& containerDiagnostics =
        snapshot.containerPlacementDiagnostics;
    FamilyCoverageDiagnostic& containers = family(output, StructuralFamily::containerPlacements);
    if (!containerDiagnostics.contextResolved && !containerDiagnostics.contextNotApplicable) {
        add_loss(containers, kFamilyCoverageLossUnread);
    }
    if (containerDiagnostics.contextResolved && containerDiagnostics.contextNotApplicable) {
        add_loss(containers, kFamilyCoverageLossPartial);
    }
    if (containerDiagnostics.unresolvedReads != 0) {
        add_loss(containers, kFamilyCoverageLossUnread);
    }
    if (container_dropped(containerDiagnostics)) {
        add_loss(containers, kFamilyCoverageLossDropped);
    }
    if (!containerDiagnostics.complete
        || (!containerDiagnostics.contextNotApplicable
            && !containerDiagnostics.identityOwnerInventoryComplete)) {
        add_loss(containers, kFamilyCoverageLossPartial);
    }
    for (const ContainerPlacementList& row : snapshot.containerPlacementLists) {
        if (!row.complete) {
            add_loss(containers, kFamilyCoverageLossPartial);
        }
    }
    for (const ContainerPlacementOwner& row : snapshot.containerPlacementOwners) {
        if (row.context == SpatialContextJoin::unresolved) {
            add_loss(containers, kFamilyCoverageLossPartial);
        }
    }
    for (const ContainerPlacement& row : snapshot.containerPlacements) {
        if (!row.complete || !row.placementIdentifierRead) {
            add_loss(containers, kFamilyCoverageLossPartial);
        }
    }
    for (const ContainerPlacementConfig& row : snapshot.containerPlacementConfigs) {
        if (!row.complete) {
            add_loss(containers, kFamilyCoverageLossPartial);
        }
    }
    const bool containerRows =
        !snapshot.containerPlacementLists.empty() || !snapshot.containerPlacementOwners.empty()
        || !snapshot.containerPlacements.empty() || !snapshot.containerPlacementConfigs.empty()
        || !snapshot.containerPlacementComponents.empty();
    if (containerDiagnostics.contextNotApplicable && containerRows) {
        add_loss(containers, kFamilyCoverageLossPartial);
    }
    finish(containers, containerRows, containerDiagnostics.semanticUnresolved != 0);

    FamilyCoverageDiagnostic& embedded = family(output, StructuralFamily::embeddedPlacements);
    inherit_loss(embedded, objects);
    const EmbeddedPlacementDiagnostics& embeddedDiagnostics = snapshot.embeddedPlacementDiagnostics;
    if (embeddedDiagnostics.unreadConfigurations != 0) {
        add_loss(embedded, kFamilyCoverageLossUnread);
    }
    if (embeddedDiagnostics.droppedLinks != 0 || embeddedDiagnostics.droppedPlacements != 0) {
        add_loss(embedded, kFamilyCoverageLossDropped);
    }
    if (embeddedDiagnostics.malformedDescriptors != 0
        || embeddedDiagnostics.malformedPlacements != 0 || !embeddedDiagnostics.complete) {
        add_loss(embedded, kFamilyCoverageLossPartial);
    }
    for (const EmbeddedPlacementLink& row : snapshot.embeddedPlacementLinks) {
        if (!row.complete) {
            add_loss(embedded, kFamilyCoverageLossPartial);
        }
    }
    const bool embeddedApplicable = embeddedDiagnostics.applicableDescriptors != 0
                                    || !snapshot.embeddedPlacementLinks.empty()
                                    || !snapshot.embeddedPlacements.empty();
    finish(embedded, embeddedApplicable, embeddedDiagnostics.unresolvedClassDefinitions != 0);

    FamilyCoverageDiagnostic& type23 = family(output, StructuralFamily::type23Placements);
    inherit_loss(type23, objects);
    inherit_loss(type23, containers);
    const Type23PlacementDiagnostics& type23Diagnostics = snapshot.type23PlacementDiagnostics;
    if (type23Diagnostics.unreadIdentifiers != 0) {
        add_loss(type23, kFamilyCoverageLossUnread);
    }
    if (type23Diagnostics.droppedLinks != 0 || type23Diagnostics.droppedCandidates != 0) {
        add_loss(type23, kFamilyCoverageLossDropped);
    }
    if (!type23Diagnostics.complete) {
        add_loss(type23, kFamilyCoverageLossPartial);
    }
    bool type23SemanticUnresolved = type23Diagnostics.zeroIdentityMatches != 0
                                    || type23Diagnostics.multipleIdentityMatches != 0
                                    || type23Diagnostics.zeroActiveCandidates != 0
                                    || type23Diagnostics.multipleActiveCandidates != 0;
    for (const Type23PlacementLink& row : snapshot.type23PlacementLinks) {
        if (!row.complete) {
            add_loss(type23, kFamilyCoverageLossPartial);
        }
        type23SemanticUnresolved = type23SemanticUnresolved || row.join != ReferenceJoin::exact;
    }
    finish(type23, !snapshot.type23PlacementLinks.empty(), type23SemanticUnresolved);

    FamilyCoverageDiagnostic& spatial = family(output, StructuralFamily::staticSpatial);
    if (!snapshot.staticSpatialContextResolved && !snapshot.staticSpatialNotApplicable) {
        add_loss(spatial, kFamilyCoverageLossUnread);
    }
    if (snapshot.staticSpatialContextResolved && snapshot.staticSpatialNotApplicable) {
        add_loss(spatial, kFamilyCoverageLossPartial);
    }
    if (snapshot.staticSpatialUnresolvedReads != 0) {
        add_loss(spatial, kFamilyCoverageLossUnread);
    }
    if (snapshot.staticSpatialDropped != 0) {
        add_loss(spatial, kFamilyCoverageLossDropped);
    }
    if (!snapshot.staticSpatialComplete) {
        add_loss(spatial, kFamilyCoverageLossPartial);
    }
    for (const StaticSpatialTable& row : snapshot.staticSpatialTables) {
        if (!row.complete) {
            add_loss(spatial, kFamilyCoverageLossPartial);
        }
    }
    const bool spatialRows = !snapshot.staticSpatialTables.empty()
                             || !snapshot.staticSpatialOwners.empty()
                             || !snapshot.staticSpatialInstances.empty();
    if (snapshot.staticSpatialNotApplicable && spatialRows) {
        add_loss(spatial, kFamilyCoverageLossPartial);
    }
    finish(spatial, spatialRows, snapshot.staticSpatialSemanticUnresolved != 0);

    FamilyCoverageDiagnostic& triggers = family(output, StructuralFamily::triggerVolumes);
    inherit_loss(triggers, objects);
    const TriggerVolumeDiagnostics& triggerDiagnostics = snapshot.triggerVolumeDiagnostics;
    if (triggerDiagnostics.unresolvedReads != 0) {
        add_loss(triggers, kFamilyCoverageLossUnread);
    }
    if (trigger_dropped(triggerDiagnostics)) {
        add_loss(triggers, kFamilyCoverageLossDropped);
    }
    if (!triggerDiagnostics.complete) {
        add_loss(triggers, kFamilyCoverageLossPartial);
    }
    bool triggerSemanticUnresolved =
        triggerDiagnostics.zeroMatches != 0 || triggerDiagnostics.multipleMatches != 0;
    for (const TriggerVolumeTable& row : snapshot.triggerVolumeTables) {
        if (!row.complete) {
            add_loss(triggers, kFamilyCoverageLossPartial);
        }
    }
    for (const TriggerVolumeOwner& row : snapshot.triggerVolumeOwners) {
        triggerSemanticUnresolved =
            triggerSemanticUnresolved || row.slotJoin != ReferenceJoin::exact;
    }
    for (const TriggerVolumeInstance& row : snapshot.triggerVolumeInstances) {
        if (!row.complete) {
            add_loss(triggers, kFamilyCoverageLossPartial);
        }
    }
    const bool triggerRows = !snapshot.triggerVolumeTables.empty()
                             || !snapshot.triggerVolumeOwners.empty()
                             || !snapshot.triggerVolumeInstances.empty();
    finish(triggers, triggerRows, triggerSemanticUnresolved);

    FamilyCoverageDiagnostic& names = family(output, StructuralFamily::names);
    bool nameSemanticUnresolved = false;
    for (const Name& row : snapshot.names) {
        if (row.strongestTierOverflow) {
            add_loss(names, kFamilyCoverageLossDropped);
        }
        nameSemanticUnresolved = nameSemanticUnresolved || row.selectedCandidate == kNoRow;
    }
    for (const TagName& row : snapshot.tagNames) {
        nameSemanticUnresolved = nameSemanticUnresolved || row.selectedCandidate == kNoRow;
    }
    const bool nameApplicable =
        !snapshot.names.empty() || !snapshot.tagNames.empty() || !snapshot.nameCandidates.empty()
        || !snapshot.inlineNameCandidates.empty() || !snapshot.inlineNameBytes.empty();
    finish(names, nameApplicable, nameSemanticUnresolved);
    return output;
}

} // namespace coverage_internal

/** Replaces the family ledger with the result derived from this exact snapshot. */
inline void refresh_coverage_diagnostics(Snapshot& snapshot) noexcept {
    snapshot.coverageDiagnostics = coverage_internal::derive(snapshot);
}

/** @return Stable text for one structural-family identifier. */
[[nodiscard]] inline const char* structural_family_name(StructuralFamily value) noexcept {
    switch (value) {
    case StructuralFamily::scenarioTopology:
        return "scenario_topology";
    case StructuralFamily::objectGraph:
        return "object_graph";
    case StructuralFamily::typedReferences:
        return "typed_references";
    case StructuralFamily::authoredPlacements:
        return "authored_placements";
    case StructuralFamily::containerPlacements:
        return "container_placements";
    case StructuralFamily::embeddedPlacements:
        return "embedded_placements";
    case StructuralFamily::type23Placements:
        return "type23_placements";
    case StructuralFamily::staticSpatial:
        return "static_spatial";
    case StructuralFamily::triggerVolumes:
        return "trigger_volumes";
    case StructuralFamily::names:
        return "names";
    case StructuralFamily::count:
        break;
    }
    return "unknown";
}

} // namespace sunrise::state::build_data::scriptables
