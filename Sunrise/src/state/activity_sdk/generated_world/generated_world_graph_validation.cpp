#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../../build_data/scriptables/inline_name_evidence.h"
#include "generated_world_graph_validation_internal.h"
#include "internal.h"

namespace sunrise::state::activity_sdk::generated_world::internal {
namespace {

namespace catalog = build_data::scriptables;
namespace inline_name_evidence = build_data::scriptables::inline_name_evidence;

/** @return True when a vector is bounded by the shard's per-section row cap. */
template <typename Value> [[nodiscard]] bool bounded_rows(const std::vector<Value>& rows) noexcept {
    return rows.size() <= static_cast<std::size_t>(format::kMaximumRowsPerSection);
}

/** @return True when every finite transform lane retains its exact package bit pattern. */
template <std::size_t Size>
[[nodiscard]] bool exact_float_bits(const std::array<float, Size>& values,
                                    const std::array<std::uint32_t, Size>& bits) noexcept {
    for (std::size_t index = 0; index < Size; ++index) {
        if (!std::isfinite(values[index])
            || std::bit_cast<std::uint32_t>(values[index]) != bits[index]) {
            return false;
        }
    }
    return true;
}

/** @return True when the raw byte bank stays under the authenticated shard file cap. */
[[nodiscard]] bool bounded_rows(const std::vector<std::byte>& rows) noexcept {
    return rows.size() <= static_cast<std::size_t>(format::kMaximumInlineNameBankBytes);
}

/** @return True when every snapshot vector is safe to traverse. */
[[nodiscard]] bool bounded_snapshot(const catalog::Snapshot& snapshot) noexcept {
    return bounded_rows(snapshot.bubbles) && bounded_rows(snapshot.states)
           && bounded_rows(snapshot.objects) && bounded_rows(snapshot.slots)
           && bounded_rows(snapshot.descriptors) && bounded_rows(snapshot.embeddedPlacementLinks)
           && bounded_rows(snapshot.embeddedPlacements) && bounded_rows(snapshot.references)
           && bounded_rows(snapshot.authoredPlacements)
           && bounded_rows(snapshot.containerPlacementLists)
           && bounded_rows(snapshot.containerPlacementOwners)
           && bounded_rows(snapshot.containerPlacements)
           && bounded_rows(snapshot.containerPlacementConfigs)
           && bounded_rows(snapshot.containerPlacementComponents)
           && bounded_rows(snapshot.type23PlacementLinks)
           && bounded_rows(snapshot.type23PlacementCandidates)
           && bounded_rows(snapshot.staticSpatialTables)
           && bounded_rows(snapshot.staticSpatialOwners)
           && bounded_rows(snapshot.staticSpatialInstances)
           && bounded_rows(snapshot.triggerVolumeTables)
           && bounded_rows(snapshot.triggerVolumeOwners)
           && bounded_rows(snapshot.triggerVolumeIncomingReferences)
           && bounded_rows(snapshot.triggerVolumeInstances)
           && bounded_rows(snapshot.triggerVolumeVertices)
           && bounded_rows(snapshot.triggerVolumeTriangles) && bounded_rows(snapshot.names)
           && bounded_rows(snapshot.tagNames) && bounded_rows(snapshot.nameCandidates)
           && bounded_rows(snapshot.inlineNameCandidates) && bounded_rows(snapshot.inlineNameBytes)
           && bounded_rows(snapshot.authoredSquadConfigContexts)
           && bounded_rows(snapshot.authoredSquadPlacementContexts)
           && bounded_rows(snapshot.authoredSquadPointContexts)
           && bounded_rows(snapshot.authoredSquadPointPlacementMatches)
           && bounded_rows(snapshot.authoredSquadEdgeContexts);
}

/** @return True when the scenario, object, slot, and descriptor ownership graph is exact. */
[[nodiscard]] bool valid_core_graph(const catalog::Snapshot& snapshot) noexcept {
    if (!canonical_ranges(snapshot.bubbles,
                          &catalog::Bubble::firstState,
                          &catalog::Bubble::stateCount,
                          snapshot.states.size())
        || !canonical_ranges(snapshot.objects,
                             &catalog::Object::firstSlot,
                             &catalog::Object::slotCount,
                             snapshot.slots.size())
        || !canonical_ranges(snapshot.slots,
                             &catalog::Slot::firstDescriptor,
                             &catalog::Slot::descriptorCount,
                             snapshot.descriptors.size())) {
        return false;
    }
    for (std::size_t row = 0; row < snapshot.states.size(); ++row) {
        const catalog::State& state = snapshot.states[row];
        if (!valid_row(state.bubbleRow, snapshot.bubbles.size())
            || !range_contains(snapshot.bubbles[state.bubbleRow].firstState,
                               snapshot.bubbles[state.bubbleRow].stateCount,
                               snapshot.states.size(),
                               row)
            || !valid_optional_row(state.nameRow, snapshot.names.size())
            || !valid_optional_row(state.entryNameRow, snapshot.tagNames.size())
            || !valid_optional_row(state.registryNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (const catalog::Bubble& bubble : snapshot.bubbles) {
        if (!valid_optional_row(bubble.nameRow, snapshot.names.size())) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.objects.size(); ++row) {
        const catalog::Object& object = snapshot.objects[row];
        if (!valid_row(object.bubbleRow, snapshot.bubbles.size())
            || !valid_row(object.stateRow, snapshot.states.size())
            || snapshot.states[object.stateRow].bubbleRow != object.bubbleRow
            || (object.placedSubblockCount == 0 && object.placedLeafCount != 0)
            || (object.placedLeafCount == 0
                && (object.placedHopCount != 0 || object.configCount != 0
                    || object.bareTargetCount != 0))
            || object.configCount > object.placedHopCount
            || object.bareTargetCount > object.placedHopCount
            || !valid_optional_row(object.registryNameRow, snapshot.tagNames.size())
            || !valid_optional_row(object.objectNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.slots.size(); ++row) {
        const catalog::Slot& slot = snapshot.slots[row];
        if (!valid_row(slot.objectRow, snapshot.objects.size())
            || !range_contains(snapshot.objects[slot.objectRow].firstSlot,
                               snapshot.objects[slot.objectRow].slotCount,
                               snapshot.slots.size(),
                               row)
            || !valid_optional_row(slot.nameRow, snapshot.names.size())) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.descriptors.size(); ++row) {
        const catalog::Descriptor& descriptor = snapshot.descriptors[row];
        if (!valid_row(descriptor.slotRow, snapshot.slots.size())
            || !range_contains(snapshot.slots[descriptor.slotRow].firstDescriptor,
                               snapshot.slots[descriptor.slotRow].descriptorCount,
                               snapshot.descriptors.size(),
                               row)
            || !valid_optional_row(descriptor.configNameRow, snapshot.tagNames.size())
            || !valid_optional_row(descriptor.placementLinkRow,
                                   snapshot.type23PlacementLinks.size())
            || !valid_optional_row(descriptor.embeddedPlacementLinkRow,
                                   snapshot.embeddedPlacementLinks.size())) {
            return false;
        }
    }
    return true;
}

/** @return True when embedded placement and object-reference edges stay in their owners. */
[[nodiscard]] bool valid_object_edges(const catalog::Snapshot& snapshot) noexcept {
    if (!canonical_ranges(snapshot.embeddedPlacementLinks,
                          &catalog::EmbeddedPlacementLink::firstCandidate,
                          &catalog::EmbeddedPlacementLink::candidateCount,
                          snapshot.embeddedPlacements.size())) {
        return false;
    }
    for (std::size_t row = 0; row < snapshot.embeddedPlacementLinks.size(); ++row) {
        const catalog::EmbeddedPlacementLink& link = snapshot.embeddedPlacementLinks[row];
        if (!valid_row(link.descriptorRow, snapshot.descriptors.size())
            || snapshot.descriptors[link.descriptorRow].embeddedPlacementLinkRow != row
            || !valid_row(link.slotRow, snapshot.slots.size())
            || snapshot.descriptors[link.descriptorRow].slotRow != link.slotRow
            || !valid_row(link.objectRow, snapshot.objects.size())
            || snapshot.slots[link.slotRow].objectRow != link.objectRow
            || link.candidateCount > link.declaredPlacementCount) {
            return false;
        }
    }
    for (std::size_t row = 0; row < snapshot.embeddedPlacements.size(); ++row) {
        const catalog::EmbeddedPlacement& placement = snapshot.embeddedPlacements[row];
        if (!valid_row(placement.linkRow, snapshot.embeddedPlacementLinks.size())) {
            return false;
        }
        const catalog::EmbeddedPlacementLink& link =
            snapshot.embeddedPlacementLinks[placement.linkRow];
        if (!range_contains(
                link.firstCandidate, link.candidateCount, snapshot.embeddedPlacements.size(), row)
            || !valid_optional_row(placement.classListNameRow, snapshot.tagNames.size())) {
            return false;
        }
    }
    for (const catalog::TypedReference& reference : snapshot.references) {
        if (!valid_row(reference.sourceObjectRow, snapshot.objects.size())
            || !valid_optional_row(reference.sourceSlotRow, snapshot.slots.size())
            || (reference.sourceSlotRow != catalog::kNoRow
                && snapshot.slots[reference.sourceSlotRow].objectRow != reference.sourceObjectRow)
            || !valid_optional_row(reference.sourceConfigNameRow, snapshot.tagNames.size())
            || !valid_optional_row(reference.targetObjectRow, snapshot.objects.size())) {
            return false;
        }
        if (reference.join == catalog::ReferenceJoin::exact) {
            if (!valid_row(reference.targetObjectRow, snapshot.objects.size())) {
                return false;
            }
            const catalog::Object& target = snapshot.objects[reference.targetObjectRow];
            const std::size_t slotRow =
                static_cast<std::size_t>(target.firstSlot) + reference.targetSlotIndex;
            if (reference.targetKey != target.registryKey
                || reference.targetSlotIndex >= target.slotCount
                || !valid_row(static_cast<std::uint32_t>(slotRow), snapshot.slots.size())
                || snapshot.slots[slotRow].slotType != reference.targetSlotType) {
                return false;
            }
        } else if (reference.targetObjectRow != catalog::kNoRow) {
            return false;
        }
    }
    for (const catalog::AuthoredPlacement& placement : snapshot.authoredPlacements) {
        if (!valid_row(placement.sourceObjectRow, snapshot.objects.size())
            || !valid_row(placement.bubbleRow, snapshot.bubbles.size())
            || !valid_row(placement.stateRow, snapshot.states.size())
            || snapshot.objects[placement.sourceObjectRow].bubbleRow != placement.bubbleRow
            || snapshot.objects[placement.sourceObjectRow].stateRow != placement.stateRow
            || !valid_optional_row(placement.objectListNameRow, snapshot.tagNames.size())
            || !valid_optional_row(placement.classListNameRow, snapshot.tagNames.size())
            || !exact_float_bits(placement.rotation, placement.rotationBits)
            || !exact_float_bits(placement.position, placement.positionBits)
            || !std::isfinite(placement.uniformScale)
            || std::bit_cast<std::uint32_t>(placement.uniformScale) != placement.uniformScaleBits) {
            return false;
        }
    }
    return true;
}

/** @return True when name candidate ranges and selected rows are locally bounded. */
[[nodiscard]] bool valid_name_graph(const catalog::Snapshot& snapshot) noexcept {
    for (const catalog::Name& name : snapshot.names) {
        if (!valid_range(name.firstCandidate, name.candidateCount, snapshot.nameCandidates.size())
            || !valid_optional_row(name.selectedCandidate, snapshot.nameCandidates.size())
            || (name.selectedCandidate != catalog::kNoRow
                && !range_contains(name.firstCandidate,
                                   name.candidateCount,
                                   snapshot.nameCandidates.size(),
                                   name.selectedCandidate))) {
            return false;
        }
    }
    for (const catalog::TagName& name : snapshot.tagNames) {
        if (!valid_range(name.firstCandidate, name.candidateCount, snapshot.nameCandidates.size())
            || !valid_optional_row(name.selectedCandidate, snapshot.nameCandidates.size())
            || (name.selectedCandidate != catalog::kNoRow
                && !range_contains(name.firstCandidate,
                                   name.candidateCount,
                                   snapshot.nameCandidates.size(),
                                   name.selectedCandidate))) {
            return false;
        }
    }
    return true;
}

/** @return True when raw inline names own canonical UTF-8 ranges and exact FNV-1 hashes. */
[[nodiscard]] bool valid_inline_name_evidence(const catalog::Snapshot& snapshot) noexcept {
    std::size_t expectedFirst = 0;
    std::span<const std::byte> priorBytes{};
    std::uint32_t priorHash = 0;
    bool hasPrior = false;
    for (const catalog::InlineNameCandidate& row : snapshot.inlineNameCandidates) {
        const std::size_t first = row.firstByte;
        const std::size_t count = row.byteCount;
        if (first != expectedFirst || count == 0 || count > catalog::kInlineNameMaximumBytes
            || first > snapshot.inlineNameBytes.size()
            || count > snapshot.inlineNameBytes.size() - first) {
            return false;
        }
        const auto bytes = std::span(snapshot.inlineNameBytes).subspan(first, count);
        if (std::find(bytes.begin(), bytes.end(), std::byte{}) != bytes.end()
            || !inline_name_evidence::valid_utf8(bytes)
            || inline_name_evidence::hash(bytes) != row.hash) {
            return false;
        }
        if (hasPrior
            && (priorHash > row.hash
                || (priorHash == row.hash
                    && !std::lexicographical_compare(
                        priorBytes.begin(), priorBytes.end(), bytes.begin(), bytes.end())))) {
            return false;
        }
        expectedFirst += count;
        priorBytes = bytes;
        priorHash = row.hash;
        hasPrior = true;
    }
    return expectedFirst == snapshot.inlineNameBytes.size();
}

} // namespace

/** @return True when one required row names an existing row. */
[[nodiscard]] bool valid_row(std::uint32_t row, std::size_t size) noexcept {
    return static_cast<std::size_t>(row) < size;
}

/** @return True when one optional row is absent or names an existing row. */
[[nodiscard]] bool valid_optional_row(std::uint32_t row, std::size_t size) noexcept {
    return row == catalog::kNoRow || valid_row(row, size);
}

/** @return True when a 32-bit half-open row range stays inside one vector. */
[[nodiscard]] bool
valid_range(std::uint32_t first, std::uint32_t count, std::size_t size) noexcept {
    const std::size_t begin = first;
    const std::size_t length = count;
    return begin <= size && length <= size - begin;
}

/** @return True when one row lies inside a checked half-open owner range. */
[[nodiscard]] bool range_contains(std::uint32_t first,
                                  std::uint32_t count,
                                  std::size_t size,
                                  std::size_t row) noexcept {
    return valid_range(first, count, size) && row >= first
           && row - static_cast<std::size_t>(first) < count;
}

/** Rejects only data that is unsafe to index or traverse. */
bool valid_snapshot_graph(const catalog::Snapshot& snapshot) noexcept {
    if (snapshot.status != catalog::BuildStatus::ready
        || snapshot.coverage != catalog::BuildCoverage::full) {
        return false;
    }
    return bounded_snapshot(snapshot) && valid_core_graph(snapshot) && valid_object_edges(snapshot)
           && valid_container_graph(snapshot) && valid_type23_graph(snapshot)
           && valid_spatial_graph(snapshot) && valid_name_graph(snapshot)
           && valid_inline_name_evidence(snapshot) && valid_authored_squad_context_graph(snapshot);
}

} // namespace sunrise::state::activity_sdk::generated_world::internal
