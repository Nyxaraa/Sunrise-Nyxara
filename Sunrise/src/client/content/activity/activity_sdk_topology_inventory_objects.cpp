#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

#include "../../../middleware/content/packages/tables/scenario_reader.h"
#include "activity_sdk_topology_inventory_internal.h"

namespace sunrise::client::content::activity::sdk_generation::topology_inventory::detail {
namespace {

namespace tables = middleware::content::packages::tables;

/** Compares normalized schema evidence in deterministic tuple order. */
[[nodiscard]] bool descriptor_less(const DescriptorEvidence& left,
                                   const DescriptorEvidence& right) noexcept {
    return std::tie(left.componentClass, left.senseSchema, left.authSchema)
           < std::tie(right.componentClass, right.senseSchema, right.authSchema);
}

/** Builds one exact canonical slot definition from one scenario occurrence. */
[[nodiscard]] bool build_slot(const catalog::Snapshot& source,
                              std::uint32_t sourceObjectRow,
                              std::uint32_t sourceSlotRow,
                              std::uint32_t objectTag,
                              std::uint32_t slotOrdinal,
                              bool descriptorEvidenceComplete,
                              std::size_t& nextDescriptor,
                              Slot& output) {
    if (sourceSlotRow >= source.slots.size()) {
        return false;
    }
    const catalog::Slot& input = source.slots[sourceSlotRow];
    if (slotOrdinal > (std::numeric_limits<std::uint16_t>::max)()
        || input.objectRow != sourceObjectRow || input.slotIndex != slotOrdinal
        || input.firstDescriptor != nextDescriptor || nextDescriptor > source.descriptors.size()
        || input.descriptorCount > source.descriptors.size() - nextDescriptor) {
        return false;
    }
    output = {};
    output.nameHash = input.nameHash;
    output.slotIndex = input.slotIndex;
    output.slotType = input.slotType;
    output.descriptorCount =
        descriptorEvidenceComplete ? input.descriptorCount : format::kAbsentIndex;
    if (!format_text(output.id,
                     "slot/%08x/%06x/%04x/%04x",
                     static_cast<unsigned>(objectTag),
                     static_cast<unsigned>(slotOrdinal),
                     static_cast<unsigned>(input.slotIndex),
                     static_cast<unsigned>(input.slotType))) {
        return false;
    }

    std::vector<std::uint32_t> components{};
    std::vector<std::uint32_t> sense{};
    std::vector<std::uint32_t> auth{};
    if (descriptorEvidenceComplete) {
        components.reserve(input.descriptorCount);
        sense.reserve(input.descriptorCount);
        auth.reserve(input.descriptorCount);
        output.descriptorEvidence.reserve(input.descriptorCount);
    }
    for (std::uint32_t ordinal = 0; ordinal < input.descriptorCount; ++ordinal) {
        const std::size_t row = nextDescriptor + ordinal;
        const catalog::Descriptor& descriptor = source.descriptors[row];
        if (descriptor.slotRow != sourceSlotRow) {
            return false;
        }
        if (descriptorEvidenceComplete) {
            output.descriptorEvidence.push_back(
                {descriptor.componentClass, descriptor.senseSchema, descriptor.authSchema});
            components.push_back(descriptor.componentClass);
            if (descriptor.senseSchema != 0 && descriptor.senseSchema != format::kAbsentIndex) {
                sense.push_back(descriptor.senseSchema);
            }
            if (descriptor.authSchema != 0 && descriptor.authSchema != format::kAbsentIndex) {
                auth.push_back(descriptor.authSchema);
            }
        }
    }
    nextDescriptor += input.descriptorCount;
    if (!descriptorEvidenceComplete) {
        return true;
    }
    std::sort(output.descriptorEvidence.begin(), output.descriptorEvidence.end(), descriptor_less);
    const auto normalize = [](std::vector<std::uint32_t>& values) {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };
    normalize(components);
    normalize(sense);
    normalize(auth);
    output.descriptorComponentClass =
        components.size() == 1 ? components.front() : format::kAbsentIndex;
    output.descriptorSenseSchema = sense.size() == 1 ? sense.front() : format::kAbsentIndex;
    output.descriptorAuthSchema = auth.size() == 1 ? auth.front() : format::kAbsentIndex;
    return true;
}

/** Compares the exact ordered declaration identity of one object slot. */
[[nodiscard]] bool same_slot_identity(const Slot& left, const Slot& right) noexcept {
    return left.nameHash == right.nameHash && left.slotIndex == right.slotIndex
           && left.slotType == right.slotType;
}

/** Compares descriptor evidence only after both package walks completed. */
[[nodiscard]] bool same_slot_descriptor_evidence(const Slot& left, const Slot& right) noexcept {
    return left.descriptorComponentClass == right.descriptorComponentClass
           && left.descriptorSenseSchema == right.descriptorSenseSchema
           && left.descriptorAuthSchema == right.descriptorAuthSchema
           && left.descriptorCount == right.descriptorCount
           && left.descriptorEvidence == right.descriptorEvidence;
}

/** Requires one exact key and ordered slot table for every repeated object tag. */
[[nodiscard]] bool same_object_identity(const Object& left, const Object& right) noexcept {
    if (left.objectTag != right.objectTag || left.objectKey != right.objectKey
        || left.slotCount != right.slotCount
        || left.definitionSlots.size() != right.definitionSlots.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.definitionSlots.size(); ++index) {
        if (!same_slot_identity(left.definitionSlots[index], right.definitionSlots[index])) {
            return false;
        }
    }
    return true;
}

/** Requires all five exact counts after both source walks completed. */
[[nodiscard]] bool same_object_count_evidence(const Object& left, const Object& right) noexcept {
    return left.countEvidenceComplete && right.countEvidenceComplete && left.pendingCountMask == 0
           && right.pendingCountMask == 0 && left.configCount == right.configCount
           && left.placedSubblockCount == right.placedSubblockCount
           && left.placedLeafCount == right.placedLeafCount
           && left.placedHopCount == right.placedHopCount
           && left.bareTargetCount == right.bareTargetCount
           && left.replicatedPlacementCount == right.replicatedPlacementCount;
}

/** Requires one exact normalized descriptor multiset after both walks completed. */
[[nodiscard]] bool same_object_descriptor_evidence(const Object& left,
                                                   const Object& right) noexcept {
    if (!left.descriptorEvidenceComplete || !right.descriptorEvidenceComplete
        || left.descriptorCount != right.descriptorCount) {
        return false;
    }
    for (std::size_t index = 0; index < left.definitionSlots.size(); ++index) {
        if (!same_slot_descriptor_evidence(left.definitionSlots[index],
                                           right.definitionSlots[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

/** Accepts only the three authored registry array descriptors. */
bool valid_registry_field(std::uint32_t value) noexcept {
    return value == tables::kRegistryFirstDescriptor || value == tables::kRegistrySecondDescriptor
           || value == tables::kRegistryThirdDescriptor;
}

/** Builds one object definition and checks every local slot and descriptor range. */
bool build_object(const catalog::Snapshot& source,
                  std::uint32_t sourceObjectRow,
                  std::size_t& nextSlot,
                  std::size_t& nextDescriptor,
                  Object& output) {
    if (sourceObjectRow >= source.objects.size()) {
        return false;
    }
    const catalog::Object& input = source.objects[sourceObjectRow];
    if (input.objectTag == 0 || input.objectTag == format::kAbsentIndex
        || input.slotCount
               > static_cast<std::uint32_t>((std::numeric_limits<std::uint16_t>::max)()) + 1U
        || input.firstSlot != nextSlot || nextSlot > source.slots.size()
        || input.slotCount > source.slots.size() - nextSlot) {
        return false;
    }
    output = {};
    output.objectTag = input.objectTag;
    output.objectKey = input.registryKey;
    output.slotCount = input.slotCount;
    output.configCount = input.configCount;
    output.placedSubblockCount = input.placedSubblockCount;
    output.placedLeafCount = input.placedLeafCount;
    output.placedHopCount = input.placedHopCount;
    output.bareTargetCount = input.bareTargetCount;
    output.replicatedPlacementCount = input.replicatedPlacementCount;
    output.descriptorEvidenceComplete = input.complete;
    output.countEvidenceComplete = input.complete;
    output.pendingCountMask = input.complete ? 0 : kObjectCountPendingMask;
    if ((output.placedSubblockCount == 0 && output.placedLeafCount != 0)
        || (output.placedLeafCount == 0
            && (output.placedHopCount != 0 || output.configCount != 0
                || output.bareTargetCount != 0))
        || output.configCount > output.placedHopCount
        || output.bareTargetCount > output.placedHopCount) {
        return false;
    }
    if (!format_text(output.id, "object/%08x", static_cast<unsigned>(input.objectTag))) {
        return false;
    }
    output.definitionSlots.reserve(input.slotCount);
    std::uint64_t descriptorCount = 0;
    for (std::uint32_t ordinal = 0; ordinal < input.slotCount; ++ordinal) {
        Slot slot{};
        if (!build_slot(source,
                        sourceObjectRow,
                        static_cast<std::uint32_t>(nextSlot + ordinal),
                        input.objectTag,
                        ordinal,
                        input.complete,
                        nextDescriptor,
                        slot)) {
            return false;
        }
        if (input.complete) {
            descriptorCount += slot.descriptorCount;
            if (descriptorCount >= format::kAbsentIndex) {
                return false;
            }
        }
        output.definitionSlots.push_back(std::move(slot));
    }
    nextSlot += input.slotCount;
    output.descriptorCount =
        input.complete ? static_cast<std::uint32_t>(descriptorCount) : format::kAbsentIndex;
    return true;
}

/** Verifies or upgrades one prior definition from one repeated occurrence. */
bool merge_object(Object input, Object& output) {
    if (!same_object_identity(output, input)) {
        return false;
    }
    if (output.descriptorEvidenceComplete && input.descriptorEvidenceComplete
        && !same_object_descriptor_evidence(output, input)) {
        return false;
    }
    if (output.countEvidenceComplete && input.countEvidenceComplete
        && !same_object_count_evidence(output, input)) {
        return false;
    }
    if (!output.descriptorEvidenceComplete && input.descriptorEvidenceComplete) {
        output.descriptorCount = input.descriptorCount;
        output.descriptorEvidenceComplete = true;
        output.definitionSlots = std::move(input.definitionSlots);
    }
    if (!output.countEvidenceComplete && input.countEvidenceComplete) {
        output.configCount = input.configCount;
        output.placedSubblockCount = input.placedSubblockCount;
        output.placedLeafCount = input.placedLeafCount;
        output.placedHopCount = input.placedHopCount;
        output.bareTargetCount = input.bareTargetCount;
        output.replicatedPlacementCount = input.replicatedPlacementCount;
        output.countEvidenceComplete = true;
        output.pendingCountMask = 0;
    }
    return true;
}

/** Adds or verifies one canonical object in sorted tag order. */
bool merge_object(Object input, std::vector<Object>& output) {
    auto found = std::lower_bound(
        output.begin(), output.end(), input.objectTag, [](const Object& row, std::uint32_t tag) {
            return row.objectTag < tag;
        });
    if (found != output.end() && found->objectTag == input.objectTag) {
        return merge_object(std::move(input), *found);
    }
    output.insert(found, std::move(input));
    return true;
}

/** Builds one exact scenario occurrence from a validated source placement. */
bool build_occurrence(const catalog::Object& input,
                      const State& state,
                      std::uint32_t scenarioTag,
                      std::uint32_t scenarioIndex,
                      std::uint32_t bubbleIndex,
                      std::uint32_t stateIndex,
                      Occurrence& output) noexcept {
    output = {};
    output.scenarioIndex = scenarioIndex;
    output.bubbleIndex = bubbleIndex;
    output.stateIndex = stateIndex;
    output.objectTag = input.objectTag;
    output.registryTag = input.registryTag;
    output.entryTag = state.entryTag;
    output.registryField = input.registryDescriptor;
    output.objectOrdinal = input.objectIndex;
    return format_text(output.id,
                       "object-occurrence/%08x/%04x/%04x/%08x/%02x/%06x/%08x",
                       static_cast<unsigned>(scenarioTag),
                       static_cast<unsigned>(state.entryIndex),
                       static_cast<unsigned>(state.stateOrdinal),
                       static_cast<unsigned>(input.registryTag),
                       static_cast<unsigned>(input.registryDescriptor),
                       static_cast<unsigned>(input.objectIndex),
                       static_cast<unsigned>(input.objectTag))
           && format_text(output.contextRegistryKey,
                          "registry-context/%08x/%04x/%04x/%08x",
                          static_cast<unsigned>(scenarioTag),
                          static_cast<unsigned>(state.entryIndex),
                          static_cast<unsigned>(state.stateOrdinal),
                          static_cast<unsigned>(input.registryTag))
           && format_text(
               output.registryId, "registry/%08x", static_cast<unsigned>(input.registryTag))
           && format_text(output.entryId, "entry/%08x", static_cast<unsigned>(state.entryTag));
}

} // namespace sunrise::client::content::activity::sdk_generation::topology_inventory::detail
