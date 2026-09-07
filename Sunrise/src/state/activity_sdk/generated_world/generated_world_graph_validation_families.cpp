#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "generated_world_graph_validation_internal.h"

namespace sunrise::state::activity_sdk::generated_world::internal {

namespace catalog = build_data::scriptables;

/** @return True when container placement, config, and component edges are exact. */
[[nodiscard]] bool valid_container_graph(const catalog::Snapshot& snapshot) noexcept {
    if (!canonical_ranges(snapshot.containerPlacements,
                          &catalog::ContainerPlacement::firstConfig,
                          &catalog::ContainerPlacement::configCount,
                          snapshot.containerPlacementConfigs.size())
        || !canonical_ranges(snapshot.containerPlacementConfigs,
                             &catalog::ContainerPlacementConfig::firstComponent,
                             &catalog::ContainerPlacementConfig::componentCount,
                             snapshot.containerPlacementComponents.size())) {
        return false;
    }
    for (const catalog::ContainerPlacementList& list : snapshot.containerPlacementLists) {
        if (!valid_optional_row(list.objectListNameRow, snapshot.tagNames.size())
            || !valid_optional_row(list.resourceNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (const catalog::ContainerPlacementOwner& owner : snapshot.containerPlacementOwners) {
        if (!valid_row(owner.listRow, snapshot.containerPlacementLists.size())
            || !valid_optional_row(owner.containerNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.containerPlacements.size(); ++row) {
        const catalog::ContainerPlacement& placement = snapshot.containerPlacements[row];
        if (!valid_row(placement.listRow, snapshot.containerPlacementLists.size())
            || snapshot.containerPlacementLists[placement.listRow].objectListTag
                   != placement.objectListTag
            || !valid_optional_row(placement.classListNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.containerPlacementConfigs.size(); ++row) {
        const catalog::ContainerPlacementConfig& config = snapshot.containerPlacementConfigs[row];
        if (!valid_row(config.placementRow, snapshot.containerPlacements.size())
            || !range_contains(snapshot.containerPlacements[config.placementRow].firstConfig,
                               snapshot.containerPlacements[config.placementRow].configCount,
                               snapshot.containerPlacementConfigs.size(),
                               row)
            || !valid_optional_row(config.configNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.containerPlacementComponents.size(); ++row) {
        const catalog::ContainerPlacementComponent& component =
            snapshot.containerPlacementComponents[row];
        if (!valid_row(component.configRow, snapshot.containerPlacementConfigs.size())
            || !range_contains(
                snapshot.containerPlacementConfigs[component.configRow].firstComponent,
                snapshot.containerPlacementConfigs[component.configRow].componentCount,
                snapshot.containerPlacementComponents.size(),
                row)) {
            return false;
        }
    }
    return true;
}

/** @return True when every type-23 join row is bounded and internally consistent. */
[[nodiscard]] bool valid_type23_graph(const catalog::Snapshot& snapshot) noexcept {
    if (!canonical_ranges(snapshot.type23PlacementLinks,
                          &catalog::Type23PlacementLink::firstCandidate,
                          &catalog::Type23PlacementLink::candidateCount,
                          snapshot.type23PlacementCandidates.size())) {
        return false;
    }
    for (std::size_t row = 0; row < snapshot.type23PlacementLinks.size(); ++row) {
        const catalog::Type23PlacementLink& link = snapshot.type23PlacementLinks[row];
        if (!valid_row(link.descriptorRow, snapshot.descriptors.size())
            || !valid_row(link.slotRow, snapshot.slots.size())
            || snapshot.descriptors[link.descriptorRow].slotRow != link.slotRow
            || snapshot.descriptors[link.descriptorRow].placementLinkRow != row
            || snapshot.descriptors[link.descriptorRow].placementIdentifier
                   != link.placementIdentifier
            || link.candidateCount > link.identityMatchCount
            || !valid_optional_row(link.resolvedCandidate,
                                   snapshot.type23PlacementCandidates.size())
            || (link.resolvedCandidate != catalog::kNoRow
                && !range_contains(link.firstCandidate,
                                   link.candidateCount,
                                   snapshot.type23PlacementCandidates.size(),
                                   link.resolvedCandidate))) {
            return false;
        }
        std::uint32_t active = 0;
        for (std::size_t candidateRow = link.firstCandidate;
             candidateRow < static_cast<std::size_t>(link.firstCandidate) + link.candidateCount;
             ++candidateRow) {
            const catalog::Type23PlacementCandidate& candidate =
                snapshot.type23PlacementCandidates[candidateRow];
            if (candidate.applicableOwnerCount != 0) {
                ++active;
            }
        }
        if (active != link.activeCandidateCount
            || (link.join == catalog::ReferenceJoin::exact
                && (!link.complete || active != 1 || link.resolvedCandidate == catalog::kNoRow
                    || snapshot.type23PlacementCandidates[link.resolvedCandidate]
                               .applicableOwnerCount
                           == 0))
            || (link.join == catalog::ReferenceJoin::ambiguous
                && (!link.complete || active <= 1 || link.resolvedCandidate != catalog::kNoRow))
            || (link.join == catalog::ReferenceJoin::unresolved
                && link.resolvedCandidate != catalog::kNoRow)) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.type23PlacementCandidates.size(); ++row) {
        const catalog::Type23PlacementCandidate& candidate =
            snapshot.type23PlacementCandidates[row];
        if (!valid_row(candidate.linkRow, snapshot.type23PlacementLinks.size())
            || !range_contains(snapshot.type23PlacementLinks[candidate.linkRow].firstCandidate,
                               snapshot.type23PlacementLinks[candidate.linkRow].candidateCount,
                               snapshot.type23PlacementCandidates.size(),
                               row)
            || !valid_row(candidate.placementRow, snapshot.containerPlacements.size())
            || !snapshot.containerPlacements[candidate.placementRow].placementIdentifierRead
            || snapshot.containerPlacements[candidate.placementRow].placementIdentifier
                   != snapshot.type23PlacementLinks[candidate.linkRow].placementIdentifier
            || !valid_optional_row(candidate.ownerRow, snapshot.containerPlacementOwners.size())
            || ((candidate.applicableOwnerCount == 0) != (candidate.ownerRow == catalog::kNoRow))
            || (candidate.ownerRow != catalog::kNoRow
                && snapshot.containerPlacementOwners[candidate.ownerRow].listRow
                       != snapshot.containerPlacements[candidate.placementRow].listRow)) {
            return false;
        }
    }
    return true;
}

/** @return True when static-spatial and trigger-volume ownership graphs are exact. */
[[nodiscard]] bool valid_spatial_graph(const catalog::Snapshot& snapshot) noexcept {
    if (!canonical_ranges(snapshot.staticSpatialTables,
                          &catalog::StaticSpatialTable::firstInstance,
                          &catalog::StaticSpatialTable::instanceCount,
                          snapshot.staticSpatialInstances.size())
        || !canonical_ranges(snapshot.triggerVolumeTables,
                             &catalog::TriggerVolumeTable::firstInstance,
                             &catalog::TriggerVolumeTable::instanceCount,
                             snapshot.triggerVolumeInstances.size())
        || !canonical_ranges(snapshot.triggerVolumeOwners,
                             &catalog::TriggerVolumeOwner::firstIncomingReference,
                             &catalog::TriggerVolumeOwner::incomingReferenceCount,
                             snapshot.triggerVolumeIncomingReferences.size())
        || !canonical_ranges(snapshot.triggerVolumeInstances,
                             &catalog::TriggerVolumeInstance::firstVertex,
                             &catalog::TriggerVolumeInstance::vertexCount,
                             snapshot.triggerVolumeVertices.size())
        || !canonical_ranges(snapshot.triggerVolumeInstances,
                             &catalog::TriggerVolumeInstance::firstTriangle,
                             &catalog::TriggerVolumeInstance::triangleCount,
                             snapshot.triggerVolumeTriangles.size())) {
        return false;
    }
    for (const catalog::StaticSpatialTable& table : snapshot.staticSpatialTables) {
        if (!valid_optional_row(table.tableNameRow, snapshot.tagNames.size())
            || !valid_optional_row(table.boundsNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (const catalog::StaticSpatialOwner& owner : snapshot.staticSpatialOwners) {
        if (!valid_row(owner.tableRow, snapshot.staticSpatialTables.size())
            || !valid_optional_row(owner.placementRow, snapshot.containerPlacements.size())
            || !valid_optional_row(owner.containerNameRow, snapshot.tagNames.size())
            || !valid_optional_row(owner.objectListNameRow, snapshot.tagNames.size())
            || !valid_optional_row(owner.parentNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.staticSpatialInstances.size(); ++row) {
        const catalog::StaticSpatialInstance& instance = snapshot.staticSpatialInstances[row];
        if (!valid_row(instance.tableRow, snapshot.staticSpatialTables.size())
            || !range_contains(snapshot.staticSpatialTables[instance.tableRow].firstInstance,
                               snapshot.staticSpatialTables[instance.tableRow].instanceCount,
                               snapshot.staticSpatialInstances.size(),
                               row)
            || !valid_optional_row(instance.resourceNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (const catalog::TriggerVolumeTable& table : snapshot.triggerVolumeTables) {
        if (!valid_optional_row(table.configNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.triggerVolumeOwners.size(); ++row) {
        const catalog::TriggerVolumeOwner& owner = snapshot.triggerVolumeOwners[row];
        if (!valid_row(owner.tableRow, snapshot.triggerVolumeTables.size())
            || !valid_row(owner.objectRow, snapshot.objects.size())
            || !valid_optional_row(owner.slotRow, snapshot.slots.size())) {
            return false;
        }
        if (owner.slotJoin == catalog::ReferenceJoin::exact) {
            if (!valid_row(owner.slotRow, snapshot.slots.size())
                || snapshot.slots[owner.slotRow].objectRow != owner.objectRow
                || snapshot.slots[owner.slotRow].slotIndex
                       != snapshot.triggerVolumeTables[owner.tableRow].slotIndex
                || snapshot.slots[owner.slotRow].slotType
                       != snapshot.triggerVolumeTables[owner.tableRow].slotType) {
                return false;
            }
        } else if (owner.slotRow != catalog::kNoRow) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.triggerVolumeIncomingReferences.size(); ++row) {
        const catalog::TriggerVolumeIncomingReference& incoming =
            snapshot.triggerVolumeIncomingReferences[row];
        if (!valid_row(incoming.ownerRow, snapshot.triggerVolumeOwners.size())
            || !range_contains(
                snapshot.triggerVolumeOwners[incoming.ownerRow].firstIncomingReference,
                snapshot.triggerVolumeOwners[incoming.ownerRow].incomingReferenceCount,
                snapshot.triggerVolumeIncomingReferences.size(),
                row)
            || !valid_row(incoming.referenceRow, snapshot.references.size())
            || !valid_row(incoming.sourceObjectRow, snapshot.objects.size())
            || !valid_row(incoming.sourceSlotRow, snapshot.slots.size())) {
            return false;
        }
        const catalog::TypedReference& reference = snapshot.references[incoming.referenceRow];
        const catalog::TriggerVolumeOwner& owner = snapshot.triggerVolumeOwners[incoming.ownerRow];
        if (reference.join != catalog::ReferenceJoin::exact
            || reference.sourceObjectRow != incoming.sourceObjectRow
            || reference.sourceSlotRow != incoming.sourceSlotRow
            || reference.targetObjectRow != owner.objectRow) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.triggerVolumeInstances.size(); ++row) {
        const catalog::TriggerVolumeInstance& instance = snapshot.triggerVolumeInstances[row];
        if (!valid_row(instance.tableRow, snapshot.triggerVolumeTables.size())
            || !range_contains(snapshot.triggerVolumeTables[instance.tableRow].firstInstance,
                               snapshot.triggerVolumeTables[instance.tableRow].instanceCount,
                               snapshot.triggerVolumeInstances.size(),
                               row)
            || !valid_optional_row(instance.classDefinitionNameRow, snapshot.tagNames.size())
            || !valid_optional_row(instance.shapeResourceNameRow, snapshot.tagNames.size())) {
            return false;
        }
        for (std::size_t triangleRow = instance.firstTriangle;
             triangleRow
             < static_cast<std::size_t>(instance.firstTriangle) + instance.triangleCount;
             ++triangleRow) {
            const catalog::TriggerVolumeTriangle& triangle =
                snapshot.triggerVolumeTriangles[triangleRow];
            if (std::any_of(triangle.indices.begin(),
                            triangle.indices.end(),
                            [&instance](std::uint8_t index) noexcept {
                                return index >= instance.vertexCount;
                            })) {
                return false;
            }
        }
    }
    return true;
}

namespace {

/** @return True when one raw float lane is finite without normalizing its package bits. */
[[nodiscard]] bool finite_bits(std::uint32_t bits) noexcept {
    return std::isfinite(std::bit_cast<float>(bits));
}

} // namespace

/** @return True when all scenario-owned authored-squad context rows are exact and canonical. */
[[nodiscard]] bool valid_authored_squad_context_graph(const catalog::Snapshot& snapshot) noexcept {
    const bool empty = snapshot.authoredSquadConfigContexts.empty()
                       && snapshot.authoredSquadPlacementContexts.empty()
                       && snapshot.authoredSquadPointContexts.empty()
                       && snapshot.authoredSquadPointPlacementMatches.empty()
                       && snapshot.authoredSquadEdgeContexts.empty();
    if (!snapshot.authoredSquadGraphContextsComplete) {
        return empty;
    }

    std::uint32_t scenarioIndex = catalog::kNoRow;
    const auto sameScenario = [&scenarioIndex](std::uint32_t value) noexcept {
        if (value == catalog::kNoRow) {
            return false;
        }
        if (scenarioIndex == catalog::kNoRow) {
            scenarioIndex = value;
        }
        return value == scenarioIndex;
    };
    std::uint32_t priorGlobal = 0;
    bool hasPrior = false;
    for (const catalog::AuthoredSquadConfigContext& row : snapshot.authoredSquadConfigContexts) {
        const bool ownsOneDefinition =
            (row.spawnerRow != catalog::kNoRow) != (row.ruleRow != catalog::kNoRow);
        if (!row.complete || !sameScenario(row.scenarioIndex) || row.globalRow == catalog::kNoRow
            || row.configTag == 0 || row.configTag == catalog::kNoRow
            || row.occurrenceIndex == catalog::kNoRow || row.objectIndex == catalog::kNoRow
            || !ownsOneDefinition || (hasPrior && priorGlobal >= row.globalRow)) {
            return false;
        }
        priorGlobal = row.globalRow;
        hasPrior = true;
    }

    priorGlobal = 0;
    hasPrior = false;
    for (const catalog::AuthoredSquadPlacementContext& row :
         snapshot.authoredSquadPlacementContexts) {
        if (!row.complete || !sameScenario(row.scenarioIndex) || row.globalRow == catalog::kNoRow
            || row.occurrenceIndex == catalog::kNoRow || row.objectListTag == 0
            || row.objectListTag == catalog::kNoRow || row.objectIndex == catalog::kNoRow
            || row.sourceOffset == 0 || !finite_bits(row.uniformScaleBits)
            || !std::all_of(row.quaternionBits.begin(), row.quaternionBits.end(), finite_bits)
            || !std::all_of(row.positionBits.begin(), row.positionBits.end(), finite_bits)
            || (hasPrior && priorGlobal >= row.globalRow)) {
            return false;
        }
        priorGlobal = row.globalRow;
        hasPrior = true;
    }

    if (!canonical_ranges(snapshot.authoredSquadPointContexts,
                          &catalog::AuthoredSquadPointContext::firstMatch,
                          &catalog::AuthoredSquadPointContext::matchCount,
                          snapshot.authoredSquadPointPlacementMatches.size())) {
        return false;
    }
    priorGlobal = 0;
    hasPrior = false;
    for (std::size_t rowIndex = 0; rowIndex < snapshot.authoredSquadPointContexts.size();
         ++rowIndex) {
        const catalog::AuthoredSquadPointContext& row =
            snapshot.authoredSquadPointContexts[rowIndex];
        if (!sameScenario(row.scenarioIndex) || row.globalRow == catalog::kNoRow
            || row.pointRow == catalog::kNoRow
            || !valid_row(row.configContextRow, snapshot.authoredSquadConfigContexts.size())
            || snapshot.authoredSquadConfigContexts[row.configContextRow].globalRow
                   != row.globalConfigContextRow
            || (hasPrior && priorGlobal >= row.globalRow)
            || (row.matchCount == 0
                && row.status != catalog::AuthoredSquadPointContextStatus::unresolved)
            || (row.matchCount == 1
                && row.status != catalog::AuthoredSquadPointContextStatus::exact)
            || (row.matchCount > 1
                && row.status != catalog::AuthoredSquadPointContextStatus::ambiguous)) {
            return false;
        }
        for (std::uint32_t ordinal = 0; ordinal < row.matchCount; ++ordinal) {
            const std::size_t matchIndex = static_cast<std::size_t>(row.firstMatch) + ordinal;
            const catalog::AuthoredSquadPointPlacementMatch& match =
                snapshot.authoredSquadPointPlacementMatches[matchIndex];
            if (!sameScenario(match.scenarioIndex)
                || match.globalRow != row.globalFirstMatch + ordinal
                || match.globalPointContextRow != row.globalRow || match.pointContextRow != rowIndex
                || match.pointRow != row.pointRow
                || match.globalConfigContextRow != row.globalConfigContextRow
                || match.configContextRow != row.configContextRow
                || !valid_row(match.placementContextRow,
                              snapshot.authoredSquadPlacementContexts.size())) {
                return false;
            }
            const catalog::AuthoredSquadPlacementContext& placement =
                snapshot.authoredSquadPlacementContexts[match.placementContextRow];
            const catalog::AuthoredSquadConfigContext& config =
                snapshot.authoredSquadConfigContexts[match.configContextRow];
            if (placement.globalRow != match.globalPlacementContextRow
                || placement.placedEntryIdentity != match.placedEntryIdentity
                || match.sameOccurrence != (placement.occurrenceIndex == config.occurrenceIndex)) {
                return false;
            }
        }
        priorGlobal = row.globalRow;
        hasPrior = true;
    }

    priorGlobal = 0;
    hasPrior = false;
    std::uint32_t priorEdge = 0;
    for (const catalog::AuthoredSquadEdgeContext& row : snapshot.authoredSquadEdgeContexts) {
        if (!sameScenario(row.scenarioIndex) || row.globalRow == catalog::kNoRow
            || row.edgeRow == catalog::kNoRow || (hasPrior && priorGlobal >= row.globalRow)
            || (hasPrior && priorEdge >= row.edgeRow)) {
            return false;
        }
        priorGlobal = row.globalRow;
        priorEdge = row.edgeRow;
        hasPrior = true;
    }
    return true;
}

} // namespace sunrise::state::activity_sdk::generated_world::internal
