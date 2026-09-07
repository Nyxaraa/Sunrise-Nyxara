#include "package_trigger_volume_marker_source.h"

#include <cstddef>

#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "authored_placement_marker.h"
#include "package_trigger_volume_geometry.h"

namespace sunrise::client::ui::activity::package_trigger_volume_marker_source {
namespace {

namespace catalog = state::build_data::scriptables;
namespace marker = authored_placement_marker;
namespace trigger_geometry = package_trigger_volume_geometry;

/** Validates the complete table-owner-instance-slot identity and returns its rows. */
[[nodiscard]] bool rows(const catalog::Snapshot& source,
                        std::uint32_t ownerRow,
                        std::uint32_t instanceRow,
                        const catalog::TriggerVolumeTable*& table,
                        const catalog::TriggerVolumeOwner*& owner,
                        const catalog::TriggerVolumeInstance*& instance,
                        const catalog::Object*& object,
                        const catalog::Slot*& slot) noexcept {
    table = nullptr;
    owner = nullptr;
    instance = nullptr;
    object = nullptr;
    slot = nullptr;
    if (ownerRow >= source.triggerVolumeOwners.size()
        || instanceRow >= source.triggerVolumeInstances.size()) {
        return false;
    }
    owner = &source.triggerVolumeOwners[ownerRow];
    if (owner->tableRow >= source.triggerVolumeTables.size()
        || owner->objectRow >= source.objects.size() || owner->slotRow >= source.slots.size()
        || owner->slotJoin != catalog::ReferenceJoin::exact || owner->slotMatchCount != 1) {
        return false;
    }
    table = &source.triggerVolumeTables[owner->tableRow];
    instance = &source.triggerVolumeInstances[instanceRow];
    object = &source.objects[owner->objectRow];
    slot = &source.slots[owner->slotRow];
    const std::size_t first = table->firstInstance;
    const std::size_t count = table->instanceCount;
    return table->complete && table->identityMatchCount == 1 && count == 1 && instanceRow == first
           && first < source.triggerVolumeInstances.size() && instance->tableRow == owner->tableRow
           && instance->complete && instance->active != 0
           && trigger_geometry::supported_transform(*instance)
           && slot->objectRow == owner->objectRow && slot->slotType == table->slotType
           && slot->slotIndex == table->slotIndex && object->registryKey == table->registryKey;
}

} // namespace

/** Builds one exact selected slot-owned trigger-volume anchor. */
bool build(const catalog::Snapshot& source,
           std::uint32_t ownerRow,
           std::uint32_t instanceRow,
           marker::Anchor& output) noexcept {
    output = {};
    const catalog::TriggerVolumeTable* table = nullptr;
    const catalog::TriggerVolumeOwner* owner = nullptr;
    const catalog::TriggerVolumeInstance* instance = nullptr;
    const catalog::Object* object = nullptr;
    const catalog::Slot* slot = nullptr;
    if (!rows(source, ownerRow, instanceRow, table, owner, instance, object, slot)
        || object->bubbleRow >= source.bubbles.size() || object->stateRow >= source.states.size()) {
        return false;
    }
    const catalog::Bubble& bubble = source.bubbles[object->bubbleRow];
    const catalog::State& state = source.states[object->stateRow];
    output.sourceKind = marker::AnchorSource::packageTriggerVolume;
    output.sourceRow = instanceRow;
    output.ownerRow = ownerRow;
    output.slotRow = owner->slotRow;
    output.bubbleRow = object->bubbleRow;
    output.bubbleIndex = bubble.index;
    output.stateRow = object->stateRow;
    output.stateEntryTag = state.entryTag;
    output.sliceSetIndex = state.sliceSetIndex;
    output.configTag = table->configTag;
    output.classListTag = instance->classDefinitionTag;
    output.entryIndex = instance->authoredRowIndex;
    output.tableRow = owner->tableRow;
    output.resourceTag = instance->shapeResourceTag;
    for (std::size_t lane = 0; lane < output.position.size(); ++lane) {
        output.boundsMinimum[lane] = instance->minimum[lane];
        output.boundsMaximum[lane] = instance->maximum[lane];
        output.position[lane] = static_cast<float>((static_cast<double>(instance->minimum[lane])
                                                    + static_cast<double>(instance->maximum[lane]))
                                                   * 0.5);
    }
    return true;
}

/** @return True while one retained owner-and-instance identity remains exact and renderable. */
bool current(const catalog::Snapshot& source, const marker::Anchor& anchor) noexcept {
    if (anchor.sourceKind != marker::AnchorSource::packageTriggerVolume) {
        return false;
    }
    marker::Anchor rebuilt{};
    return build(source, anchor.ownerRow, anchor.sourceRow, rebuilt)
           && rebuilt.slotRow == anchor.slotRow && rebuilt.bubbleRow == anchor.bubbleRow
           && rebuilt.stateRow == anchor.stateRow && rebuilt.configTag == anchor.configTag
           && rebuilt.classListTag == anchor.classListTag && rebuilt.entryIndex == anchor.entryIndex
           && rebuilt.tableRow == anchor.tableRow && rebuilt.resourceTag == anchor.resourceTag
           && rebuilt.position == anchor.position && rebuilt.boundsMinimum == anchor.boundsMinimum
           && rebuilt.boundsMaximum == anchor.boundsMaximum;
}

/** @return The owning slot's strongest hash-name row, when the exact owner remains current. */
std::uint32_t slot_name_row(const catalog::Snapshot& source,
                            const marker::Anchor& anchor) noexcept {
    if (!current(source, anchor) || anchor.slotRow >= source.slots.size()) {
        return catalog::kNoRow;
    }
    return source.slots[anchor.slotRow].nameRow;
}

/** @return The source type-31 slot name row when exactly one incoming reference exists. */
std::uint32_t trigger_name_row(const catalog::Snapshot& source,
                               const marker::Anchor& anchor) noexcept {
    if (!current(source, anchor) || anchor.ownerRow >= source.triggerVolumeOwners.size()) {
        return catalog::kNoRow;
    }
    const catalog::TriggerVolumeOwner& owner = source.triggerVolumeOwners[anchor.ownerRow];
    if (owner.incomingReferenceMatchCount != 1 || owner.incomingReferenceCount != 1
        || owner.firstIncomingReference >= source.triggerVolumeIncomingReferences.size()) {
        return catalog::kNoRow;
    }
    const catalog::TriggerVolumeIncomingReference& incoming =
        source.triggerVolumeIncomingReferences[owner.firstIncomingReference];
    if (incoming.ownerRow != anchor.ownerRow || incoming.referenceRow >= source.references.size()
        || incoming.sourceObjectRow >= source.objects.size()
        || incoming.sourceSlotRow >= source.slots.size()) {
        return catalog::kNoRow;
    }
    const catalog::TypedReference& reference = source.references[incoming.referenceRow];
    const catalog::Slot& slot = source.slots[incoming.sourceSlotRow];
    const catalog::TriggerVolumeTable& table = source.triggerVolumeTables[owner.tableRow];
    return reference.join == catalog::ReferenceJoin::exact
                   && reference.sourceObjectRow == incoming.sourceObjectRow
                   && reference.sourceSlotRow == incoming.sourceSlotRow
                   && reference.targetObjectRow == owner.objectRow
                   && reference.targetKey == table.registryKey
                   && reference.targetSlotType == table.slotType
                   && reference.targetSlotIndex == table.slotIndex
                   && slot.objectRow == incoming.sourceObjectRow && slot.slotType == 31
               ? slot.nameRow
               : catalog::kNoRow;
}

} // namespace sunrise::client::ui::activity::package_trigger_volume_marker_source
