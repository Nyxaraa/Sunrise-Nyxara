#include "package_type23_placement_marker_source.h"

#include "../../../middleware/content/packages/tables/type23_placement_identifier_reader.h"
#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "authored_placement_marker.h"

namespace sunrise::client::ui::activity::package_type23_placement_marker_source {
namespace {

namespace catalog = state::build_data::scriptables;
namespace marker = authored_placement_marker;
namespace tables = middleware::content::packages::tables;

struct Rows final {
    const catalog::Type23PlacementLink* link{};
    const catalog::Type23PlacementCandidate* candidate{};
    const catalog::Descriptor* descriptor{};
    const catalog::Slot* slot{};
    const catalog::Object* object{};
    const catalog::ContainerPlacement* placement{};
    const catalog::ContainerPlacementOwner* owner{};
};

/** Validates one complete descriptor/candidate/owner chain. */
[[nodiscard]] bool
rows(const catalog::Snapshot& source, std::uint32_t linkRow, Rows& output) noexcept {
    output = {};
    if (linkRow >= source.type23PlacementLinks.size()) {
        return false;
    }
    output.link = &source.type23PlacementLinks[linkRow];
    if (!output.link->complete || output.link->join != catalog::ReferenceJoin::exact
        || output.link->activeCandidateCount != 1
        || output.link->resolvedCandidate >= source.type23PlacementCandidates.size()
        || output.link->descriptorRow >= source.descriptors.size()
        || output.link->slotRow >= source.slots.size()) {
        return false;
    }
    output.candidate = &source.type23PlacementCandidates[output.link->resolvedCandidate];
    output.descriptor = &source.descriptors[output.link->descriptorRow];
    output.slot = &source.slots[output.link->slotRow];
    if (output.candidate->linkRow != linkRow || output.candidate->applicableOwnerCount == 0
        || output.candidate->ownerRow >= source.containerPlacementOwners.size()
        || output.candidate->placementRow >= source.containerPlacements.size()
        || output.descriptor->placementLinkRow != linkRow
        || output.descriptor->slotRow != output.link->slotRow
        || !output.descriptor->placementIdentifierRead
        || output.descriptor->placementIdentifier != output.link->placementIdentifier
        || output.descriptor->componentClass != tables::kType23ComponentClass
        || output.slot->slotType != 23 || output.slot->objectRow >= source.objects.size()) {
        return false;
    }
    output.object = &source.objects[output.slot->objectRow];
    output.placement = &source.containerPlacements[output.candidate->placementRow];
    output.owner = &source.containerPlacementOwners[output.candidate->ownerRow];
    // The client binds by scanning live objects for the identity, so the driven object need not
    // share a bubble with the sensor. The owner only has to load in this scenario.
    const bool ownerLoads =
        catalog::container_placement_owner_applies(*output.owner, output.object->bubbleRow)
        || (output.owner->context == catalog::SpatialContextJoin::packageStemBubble
            && output.owner->scenarioBubbleMask != 0);
    return output.placement->placementIdentifierRead
           && output.placement->placementIdentifier == output.link->placementIdentifier
           && output.placement->listRow == output.owner->listRow && ownerLoads;
}

} // namespace

/** Builds one exact selected type-23 slot-to-position anchor. */
bool build(const catalog::Snapshot& source,
           std::uint32_t linkRow,
           marker::Anchor& output) noexcept {
    output = {};
    Rows found{};
    if (!rows(source, linkRow, found) || found.object->bubbleRow >= source.bubbles.size()
        || found.object->stateRow >= source.states.size()) {
        return false;
    }
    const catalog::Bubble& bubble = source.bubbles[found.object->bubbleRow];
    const catalog::State& state = source.states[found.object->stateRow];
    output.sourceKind = marker::AnchorSource::packageType23Placement;
    output.sourceRow = linkRow;
    output.ownerRow = found.candidate->ownerRow;
    output.slotRow = found.link->slotRow;
    output.bubbleRow = found.object->bubbleRow;
    output.bubbleIndex = bubble.index;
    output.stateRow = found.object->stateRow;
    output.stateEntryTag = state.entryTag;
    output.sliceSetIndex = state.sliceSetIndex;
    output.objectListTag = found.placement->objectListTag;
    output.configTag = found.descriptor->configTag;
    output.classListTag = found.placement->classListTag;
    output.entryIndex = found.placement->entryIndex;
    output.ownerMatchCount = found.candidate->applicableOwnerCount;
    output.scenarioBubbleMask = found.owner->scenarioBubbleMask;
    output.placementIdentifier = found.link->placementIdentifier;
    output.position = found.placement->position;
    return true;
}

/** @return True while one retained descriptor-link identity remains exact and renderable. */
bool current(const catalog::Snapshot& source, const marker::Anchor& anchor) noexcept {
    if (anchor.sourceKind != marker::AnchorSource::packageType23Placement) {
        return false;
    }
    marker::Anchor rebuilt{};
    return build(source, anchor.sourceRow, rebuilt) && rebuilt.ownerRow == anchor.ownerRow
           && rebuilt.slotRow == anchor.slotRow && rebuilt.bubbleRow == anchor.bubbleRow
           && rebuilt.stateRow == anchor.stateRow && rebuilt.objectListTag == anchor.objectListTag
           && rebuilt.configTag == anchor.configTag && rebuilt.classListTag == anchor.classListTag
           && rebuilt.entryIndex == anchor.entryIndex
           && rebuilt.ownerMatchCount == anchor.ownerMatchCount
           && rebuilt.scenarioBubbleMask == anchor.scenarioBubbleMask
           && rebuilt.placementIdentifier == anchor.placementIdentifier
           && rebuilt.position == anchor.position;
}

/** @return The exact linked slot's strongest hash-name row. */
std::uint32_t slot_name_row(const catalog::Snapshot& source,
                            const marker::Anchor& anchor) noexcept {
    return current(source, anchor) && anchor.slotRow < source.slots.size()
               ? source.slots[anchor.slotRow].nameRow
               : catalog::kNoRow;
}

} // namespace sunrise::client::ui::activity::package_type23_placement_marker_source
