#include "scriptable_override_route.h"

#include <cstddef>
#include <string_view>

#include "../../middleware/bap/activity_message/scriptable_auth_body.h"
#include "../../middleware/bap/activity_message/squad_auth_body.h"
#include "../../middleware/content/packages/tables/region_reader.h"
#include "../../middleware/content/packages/tables/slot_descriptor_reader.h"
#include "../../state/activity/runtime.h"
#include "../../state/build_data/runtime.h"
#include "../../state/build_data/scenarios/definition.h"

namespace sunrise::server::activity::scriptable_override {
namespace {

namespace auth = middleware::bap::activity_message::scriptable_auth;
namespace squad = middleware::bap::activity_message::squad_auth;
namespace catalog = state::build_data::scriptables;
namespace layouts = state::build_data::scenarios;
namespace tables = middleware::content::packages::tables;

/** Reduces every descriptor on one slot to one unambiguous roster flag byte. */
[[nodiscard]] bool slot_flags(const catalog::Snapshot& snapshot,
                              const catalog::Slot& slot,
                              std::uint8_t& flags) noexcept {
    flags = 0;
    if (slot.descriptorCount == 0 || slot.firstDescriptor > snapshot.descriptors.size()
        || slot.descriptorCount > snapshot.descriptors.size() - slot.firstDescriptor) {
        return false;
    }
    bool first = true;
    for (std::uint32_t offset = 0; offset < slot.descriptorCount; ++offset) {
        const catalog::Descriptor& descriptor = snapshot.descriptors[slot.firstDescriptor + offset];
        std::uint8_t current = 0;
        if (descriptor.authSchema != tables::kAbsentSchema) {
            current |= layouts::kSlotAuthFlag;
        }
        if (descriptor.senseSchema != tables::kAbsentSchema) {
            current |= layouts::kSlotSenseFlag;
        }
        if (!first && current != flags) {
            return false;
        }
        flags = current;
        first = false;
    }
    return true;
}

/** @return True when the selected row has one exact supported auth schema. */
[[nodiscard]] bool supported_schema(const catalog::Snapshot& snapshot,
                                    const catalog::Slot& slot,
                                    std::uint32_t& schema) noexcept {
    schema = 0;
    if (slot.descriptorCount == 0 || slot.firstDescriptor > snapshot.descriptors.size()
        || slot.descriptorCount > snapshot.descriptors.size() - slot.firstDescriptor) {
        return false;
    }
    for (std::uint32_t offset = 0; offset < slot.descriptorCount; ++offset) {
        const catalog::Descriptor& descriptor = snapshot.descriptors[slot.firstDescriptor + offset];
        const std::uint32_t current = descriptor.authSchema;
        if (current == tables::kAbsentSchema || (offset != 0 && current != schema)) {
            return false;
        }
        if (slot.slotType == auth::kType2SlotType
            && (descriptor.componentClass != auth::kType2ComponentClass
                || descriptor.senseSchema != auth::kType2SenseSchema
                || descriptor.authSchema != auth::kType2Schema)) {
            return false;
        }
        schema = current;
    }
    return (slot.slotType == auth::kType2SlotType && schema == auth::kType2Schema)
           || (slot.slotType == squad::kSlotType && schema == squad::kSchema)
           || (slot.slotType == auth::kType23SlotType && schema == auth::kType23Schema)
           || (slot.slotType == auth::kType31SlotType && schema == auth::kType31Schema);
}

/** Matches descriptor-bearing slots to the roster's compressed wire order. */
[[nodiscard]] bool match_group(const catalog::Snapshot& snapshot,
                               const catalog::Object& object,
                               std::uint32_t objectRow,
                               const layouts::RosterGroup& group,
                               std::uint32_t selectedSlotRow,
                               std::uint16_t* selectedOffset) noexcept {
    if (!object.complete || object.registryKey != group.registryKey
        || object.firstSlot > snapshot.slots.size()
        || object.slotCount > snapshot.slots.size() - object.firstSlot) {
        return false;
    }
    std::size_t groupOffset = 0;
    bool selected = selectedOffset == nullptr;
    for (std::uint32_t offset = 0; offset < object.slotCount; ++offset) {
        const std::uint32_t slotRow = object.firstSlot + offset;
        const catalog::Slot& slot = snapshot.slots[slotRow];
        if (slot.objectRow != objectRow || slot.firstDescriptor > snapshot.descriptors.size()) {
            return false;
        }
        if (slot.descriptorCount == 0) {
            continue;
        }
        std::uint8_t flags = 0;
        if (groupOffset >= group.slotCount || !slot_flags(snapshot, slot, flags)
            || slot.slotIndex != group.slotIndices[groupOffset]
            || slot.slotType != group.slotTypes[groupOffset]
            || flags != group.slotFlags[groupOffset]) {
            return false;
        }
        if (selectedOffset != nullptr && slotRow == selectedSlotRow) {
            *selectedOffset = static_cast<std::uint16_t>(groupOffset);
            selected = true;
        }
        ++groupOffset;
    }
    return groupOffset == group.slotCount && selected;
}

/** @return True when every descriptor-bearing slot matches the compressed roster row. */
[[nodiscard]] bool same_group(const catalog::Snapshot& snapshot,
                              const catalog::Object& object,
                              std::uint32_t objectRow,
                              const layouts::RosterGroup& group) noexcept {
    return match_group(snapshot, object, objectRow, group, catalog::kNoRow, nullptr);
}

/** Tests one canonical row and records whether it is active in the current bubble. */
void consider_group(const catalog::Snapshot& snapshot,
                    const catalog::Object& object,
                    std::uint32_t objectRow,
                    std::uint16_t tableIndex,
                    bool active,
                    std::size_t& matches,
                    std::size_t& activeMatches,
                    std::uint16_t& selected,
                    std::uint32_t& selectedObjectTag) noexcept {
    layouts::RosterGroup group{};
    if (!state::build_data::find_roster_group(tableIndex, group)
        || !same_group(snapshot, object, objectRow, group)) {
        return;
    }
    ++matches;
    if (active) {
        ++activeMatches;
        selected = tableIndex;
        selectedObjectTag = group.objectTag;
    }
}

} // namespace

/** Joins one package-derived slot to an existing canonical roster row. */
bool resolve(const state::activity::SessionBinding& binding,
             const catalog::Snapshot& snapshot,
             std::uint32_t objectRow,
             std::uint32_t slotRow,
             std::int32_t effectiveRegion,
             Resolution& output) noexcept {
    output = {};
    if (!state::activity::binding_matches(binding)) {
        output.eligibility = Eligibility::staleBinding;
        return false;
    }
    if (objectRow >= snapshot.objects.size() || slotRow >= snapshot.slots.size()) {
        output.eligibility = Eligibility::incomplete;
        return false;
    }
    const catalog::Object& object = snapshot.objects[objectRow];
    const catalog::Slot& slot = snapshot.slots[slotRow];
    if (!object.complete || slot.objectRow != objectRow) {
        output.eligibility = Eligibility::incomplete;
        return false;
    }
    const bool stable = object.safety == catalog::GroupSafety::destinationSafe
                        || object.safety == catalog::GroupSafety::bubbleSafe;
    if (!stable) {
        output.eligibility = Eligibility::unstable;
        return false;
    }
    std::uint32_t schema = 0;
    if (!supported_schema(snapshot, slot, schema)) {
        output.eligibility = Eligibility::unsupportedSchema;
        return false;
    }
    if (effectiveRegion < 0) {
        output.eligibility = Eligibility::noEffectiveRegion;
        return false;
    }
    const auto& destination = binding.destination;
    const std::string_view name(reinterpret_cast<const char*>(destination.packageName.data()),
                                destination.packageNameLength);
    layouts::Definition layout{};
    if (!state::build_data::find_scenario_layout(name, layout) || snapshot.scenarioTag != layout.tag
        || std::string_view(snapshot.scenarioName.data(), snapshot.scenarioNameLength) != name) {
        output.eligibility = Eligibility::wrongScenario;
        return false;
    }

    const std::uint32_t bubble =
        static_cast<std::uint32_t>(effectiveRegion) / tables::kSliceSetIndexFactor;
    std::size_t matches = 0;
    std::size_t activeMatches = 0;
    std::uint16_t tableIndex = 0;
    std::uint32_t canonicalObjectTag = 0;
    for (std::size_t index = 0; index < layout.rosterGroupCount; ++index) {
        consider_group(snapshot,
                       object,
                       objectRow,
                       layout.rosterGroups[index],
                       true,
                       matches,
                       activeMatches,
                       tableIndex,
                       canonicalObjectTag);
    }
    for (std::size_t index = 0; index < layout.bubbleGroupCount; ++index) {
        const bool active = bubble < layouts::kBubbleCapacity
                            && (layout.bubbleGroupMasks[index] & (std::uint64_t{1} << bubble)) != 0;
        consider_group(snapshot,
                       object,
                       objectRow,
                       layout.bubbleGroups[index],
                       active,
                       matches,
                       activeMatches,
                       tableIndex,
                       canonicalObjectTag);
    }
    if (matches == 0) {
        output.eligibility = Eligibility::noRosterGroup;
        return false;
    }
    if (matches != 1 || activeMatches > 1) {
        output.eligibility = Eligibility::ambiguousRosterGroup;
        return false;
    }
    if (activeMatches == 0) {
        output.eligibility = Eligibility::inactiveBubble;
        return false;
    }
    layouts::RosterGroup selectedGroup{};
    std::uint16_t slotOffset = 0;
    if (!state::build_data::find_roster_group(tableIndex, selectedGroup)
        || !match_group(snapshot, object, objectRow, selectedGroup, slotRow, &slotOffset)) {
        output.eligibility = Eligibility::incomplete;
        return false;
    }
    // The object tag is off-wire provenance. Scenario roster dedup is key plus full layout, so an
    // alias tag may still resolve; transport matching must carry the canonical roster row's tag.
    output.target.objectTag = canonicalObjectTag;
    output.target.registryKey = object.registryKey;
    output.target.authSchema = schema;
    output.target.rosterGroupIndex = tableIndex;
    output.target.rosterSlotOffset = slotOffset;
    output.target.slotIndex = slot.slotIndex;
    output.target.slotType = static_cast<std::uint8_t>(slot.slotType);
    output.eligibility = Eligibility::eligible;
    return true;
}

/** @return Stable UI explanation for one eligibility result. */
const char* eligibility_name(Eligibility value) noexcept {
    switch (value) {
    case Eligibility::eligible:
        return "ready: exact roster group and required bubble";
    case Eligibility::staleBinding:
        return "disabled: selected activity generation is stale";
    case Eligibility::wrongScenario:
        return "disabled: catalog is not the selected destination's exact scenario";
    case Eligibility::incomplete:
        return "disabled: object or descriptor coverage is incomplete";
    case Eligibility::unstable:
        return "disabled: object is state-only, ambiguous, or not stable";
    case Eligibility::unsupportedSchema:
        return "disabled: slot has no single supported type-1/type-23/type-31 auth schema";
    case Eligibility::noEffectiveRegion:
        return "disabled: required region is unavailable";
    case Eligibility::noRosterGroup:
        return "disabled: full object layout is absent from the canonical msg-5 roster";
    case Eligibility::inactiveBubble:
        return "disabled: roster group is not registered in the required bubble";
    case Eligibility::ambiguousRosterGroup:
        return "disabled: full object layout maps to more than one canonical roster row";
    }
    return "disabled";
}

} // namespace sunrise::server::activity::scriptable_override
