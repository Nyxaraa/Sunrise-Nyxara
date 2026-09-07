#include "package_embedded_placement_marker_source.h"

#include "../../../middleware/content/packages/tables/descriptor_embedded_placement_reader.h"
#include "../../../state/build_data/scriptables/definition.h"
#include "authored_placement_marker.h"

namespace sunrise::client::ui::activity::package_embedded_placement_marker_source {
namespace {

namespace catalog = state::build_data::scriptables;
namespace marker = authored_placement_marker;
namespace tables = middleware::content::packages::tables;

struct Rows final {
    const catalog::EmbeddedPlacementLink* link{};
    const catalog::EmbeddedPlacement* placement{};
    const catalog::Descriptor* descriptor{};
    const catalog::Slot* slot{};
    const catalog::Object* object{};
};

/** Validates one exact single-candidate descriptor, slot, object, and placement chain. */
[[nodiscard]] bool
rows(const catalog::Snapshot& source, std::uint32_t linkRow, Rows& output) noexcept {
    output = {};
    if (linkRow >= source.embeddedPlacementLinks.size()) {
        return false;
    }
    output.link = &source.embeddedPlacementLinks[linkRow];
    if (!output.link->complete || output.link->declaredPlacementCount != 1
        || output.link->candidateCount != 1
        || output.link->firstCandidate >= source.embeddedPlacements.size()
        || output.link->descriptorRow >= source.descriptors.size()
        || output.link->slotRow >= source.slots.size()
        || output.link->objectRow >= source.objects.size()) {
        return false;
    }
    output.placement = &source.embeddedPlacements[output.link->firstCandidate];
    output.descriptor = &source.descriptors[output.link->descriptorRow];
    output.slot = &source.slots[output.link->slotRow];
    output.object = &source.objects[output.link->objectRow];
    const std::size_t firstDescriptor = output.slot->firstDescriptor;
    const std::size_t descriptorCount = output.slot->descriptorCount;
    const std::size_t firstSlot = output.object->firstSlot;
    const std::size_t slotCount = output.object->slotCount;
    return output.placement->linkRow == linkRow && output.placement->entryIndex == 0
           && output.placement->sourceOffset == output.link->arrayDataOffset
           && output.descriptor->embeddedPlacementLinkRow == linkRow
           && output.descriptor->slotRow == output.link->slotRow
           && output.descriptor->componentClass
                  == tables::kDescriptorEmbeddedPlacementDescriptorClass
           && output.slot->objectRow == output.link->objectRow
           && output.slot->slotType == tables::kDescriptorEmbeddedPlacementSlotType
           && firstDescriptor <= output.link->descriptorRow
           && descriptorCount <= source.descriptors.size() - firstDescriptor
           && output.link->descriptorRow - firstDescriptor < descriptorCount
           && firstSlot <= output.link->slotRow && slotCount <= source.slots.size() - firstSlot
           && output.link->slotRow - firstSlot < slotCount;
}

} // namespace

/** Builds one exact selected type-4 descriptor-to-position anchor. */
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
    if (state.bubbleRow != found.object->bubbleRow
        || found.descriptor->bubbleIndex != bubble.index) {
        return false;
    }
    output.sourceKind = marker::AnchorSource::packageEmbeddedPlacement;
    output.sourceRow = linkRow;
    output.ownerRow = found.link->objectRow;
    output.slotRow = found.link->slotRow;
    output.bubbleRow = found.object->bubbleRow;
    output.bubbleIndex = bubble.index;
    output.stateRow = found.object->stateRow;
    output.stateEntryTag = state.entryTag;
    output.sliceSetIndex = state.sliceSetIndex;
    output.configTag = found.descriptor->configTag;
    output.classListTag = found.placement->classListTag;
    output.entryIndex = found.placement->entryIndex;
    output.placementIdentifier = found.placement->identifier;
    output.position = found.placement->position;
    return true;
}

/** @return True while one retained descriptor identity remains exact and renderable. */
bool current(const catalog::Snapshot& source, const marker::Anchor& anchor) noexcept {
    if (anchor.sourceKind != marker::AnchorSource::packageEmbeddedPlacement) {
        return false;
    }
    marker::Anchor rebuilt{};
    return build(source, anchor.sourceRow, rebuilt) && rebuilt.ownerRow == anchor.ownerRow
           && rebuilt.slotRow == anchor.slotRow && rebuilt.bubbleRow == anchor.bubbleRow
           && rebuilt.bubbleIndex == anchor.bubbleIndex && rebuilt.stateRow == anchor.stateRow
           && rebuilt.stateEntryTag == anchor.stateEntryTag
           && rebuilt.sliceSetIndex == anchor.sliceSetIndex && rebuilt.configTag == anchor.configTag
           && rebuilt.classListTag == anchor.classListTag && rebuilt.entryIndex == anchor.entryIndex
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

} // namespace sunrise::client::ui::activity::package_embedded_placement_marker_source
