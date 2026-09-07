#include "scriptable_catalog_finalize.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "../../../middleware/content/packages/tables/slot_descriptor_reader.h"

namespace sunrise::client::content::activity::scriptables::internal {
namespace {

namespace catalog = state::build_data::scriptables;
namespace tables = middleware::content::packages::tables;

[[nodiscard]] bool cancelled(CancelCheck check) noexcept {
    return check != nullptr && check();
}

/** Compares schema presence and exact schema ids without assigning schema semantics. */
[[nodiscard]] bool compatible_schema(std::uint32_t left, std::uint32_t right) noexcept {
    const bool leftPresent = left != tables::kAbsentSchema;
    const bool rightPresent = right != tables::kAbsentSchema;
    return leftPresent == rightPresent && (!leftPresent || left == right);
}

/** Compares one slot's declared identity and its normalized descriptor shapes. */
[[nodiscard]] bool same_slot(const catalog::Snapshot& output,
                             const catalog::Slot& left,
                             const catalog::Slot& right,
                             CancelCheck cancel) noexcept {
    if (left.slotIndex != right.slotIndex || left.slotType != right.slotType
        || left.nameHash != right.nameHash || left.descriptorCount != right.descriptorCount) {
        return false;
    }
    for (std::uint32_t index = 0; index < left.descriptorCount; ++index) {
        if (cancelled(cancel)) {
            return false;
        }
        const catalog::Descriptor& a = output.descriptors[left.firstDescriptor + index];
        const catalog::Descriptor& b = output.descriptors[right.firstDescriptor + index];
        if (a.componentClass != b.componentClass || !compatible_schema(a.senseSchema, b.senseSchema)
            || !compatible_schema(a.authSchema, b.authSchema)) {
            return false;
        }
    }
    return true;
}

/** Compares exact ordered slot identities and reports cancellation separately. */
[[nodiscard]] bool same_layout(const catalog::Snapshot& output,
                               const catalog::Object& left,
                               const catalog::Object& right,
                               CancelCheck cancel,
                               bool& same) noexcept {
    same = false;
    if (left.slotCount != right.slotCount) {
        return true;
    }
    for (std::uint32_t index = 0; index < left.slotCount; ++index) {
        if (cancelled(cancel)) {
            return false;
        }
        const catalog::Slot& a = output.slots[left.firstSlot + index];
        const catalog::Slot& b = output.slots[right.firstSlot + index];
        if (!same_slot(output, a, b, cancel)) {
            return !cancelled(cancel);
        }
    }
    same = true;
    return true;
}

/** Object rows grouped by the registry key every identity comparison filters on first. */
using KeyIndex = std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>;

/**
 * Groups object rows by registry key.
 * The identity passes scan per key and discard everything else, so scanning the whole snapshot
 * once per query is quadratic. The grouping is built once and each scan then reads one bucket.
 */
[[nodiscard]] bool build_key_index(const catalog::Snapshot& output, KeyIndex& index) noexcept {
    try {
        index.reserve(output.objects.size());
        for (std::size_t row = 0; row < output.objects.size(); ++row) {
            index[output.objects[row].registryKey].push_back(static_cast<std::uint32_t>(row));
        }
    } catch (...) {
        return false;
    }
    return true;
}

/** @return The rows sharing one registry key, or an empty view when none do. */
[[nodiscard]] std::span<const std::uint32_t> keyed_rows(const KeyIndex& index,
                                                        std::uint32_t key) noexcept {
    const auto found = index.find(key);
    return found == index.end() ? std::span<const std::uint32_t>{} : std::span(found->second);
}

/** Counts complete placements with the same identity in one state. */
[[nodiscard]] bool matching_objects(const catalog::Snapshot& output,
                                    const KeyIndex& index,
                                    const catalog::Object& source,
                                    std::uint32_t stateRow,
                                    CancelCheck cancel,
                                    std::size_t& count) noexcept {
    count = 0;
    for (const std::uint32_t row : keyed_rows(index, source.registryKey)) {
        if (cancelled(cancel)) {
            return false;
        }
        const catalog::Object& candidate = output.objects[row];
        if (candidate.stateRow != stateRow
            || candidate.registryDescriptor != source.registryDescriptor || !candidate.complete) {
            continue;
        }
        bool same = false;
        if (!same_layout(output, source, candidate, cancel, same)) {
            return false;
        }
        if (same) {
            ++count;
        }
    }
    return true;
}

} // namespace

/** Joins typed references only when one exact target row is present. */
bool join_references(catalog::Snapshot& output, CancelCheck cancel) noexcept {
    if (cancelled(cancel)) {
        return false;
    }
    KeyIndex index{};
    if (!build_key_index(output, index)) {
        return false;
    }
    for (catalog::TypedReference& reference : output.references) {
        if (cancelled(cancel)) {
            return false;
        }
        reference.targetObjectRow = catalog::kNoRow;
        reference.join = catalog::ReferenceJoin::unresolved;
        if (reference.sourceObjectRow >= output.objects.size()) {
            continue;
        }
        const catalog::Object& source = output.objects[reference.sourceObjectRow];
        std::uint32_t joined = catalog::kNoRow;
        std::size_t matches = 0;
        for (const std::uint32_t objectRow : keyed_rows(index, reference.targetKey)) {
            if (cancelled(cancel)) {
                return false;
            }
            const catalog::Object& target = output.objects[objectRow];
            if (target.stateRow != source.stateRow || target.registryTag != source.registryTag
                || !target.complete || reference.targetSlotIndex >= target.slotCount) {
                continue;
            }
            const catalog::Slot& slot = output.slots[target.firstSlot + reference.targetSlotIndex];
            const bool descriptorBacked = slot.descriptorCount != 0;
            const bool descriptorlessTriggerVolume = slot.slotType == 60;
            if (slot.slotType == reference.targetSlotType
                && (descriptorBacked || descriptorlessTriggerVolume)) {
                joined = objectRow;
                ++matches;
            }
        }
        if (matches == 1) {
            reference.targetObjectRow = joined;
            reference.join = catalog::ReferenceJoin::exact;
        } else if (matches > 1) {
            reference.join = catalog::ReferenceJoin::ambiguous;
        }
    }
    return true;
}

/** Classifies package presence across states for each complete object identity. */
bool classify_presence(catalog::Snapshot& output, CancelCheck cancel) noexcept {
    if (cancelled(cancel)) {
        return false;
    }
    KeyIndex index{};
    if (!build_key_index(output, index)) {
        return false;
    }
    const bool walkComplete =
        output.unresolvedReads == 0
        && std::all_of(output.states.begin(),
                       output.states.end(),
                       [](const catalog::State& state) noexcept { return state.resolved; });
    for (catalog::Object& object : output.objects) {
        if (cancelled(cancel)) {
            return false;
        }
        if (object.slotCount == 0) {
            object.safety = catalog::GroupSafety::notApplicable;
            continue;
        }
        if (!walkComplete || !object.complete) {
            object.safety = catalog::GroupSafety::incomplete;
            continue;
        }
        std::size_t matches = 0;
        if (!matching_objects(output, index, object, object.stateRow, cancel, matches)) {
            return false;
        }
        if (matches != 1) {
            object.safety = catalog::GroupSafety::ambiguous;
            continue;
        }
        bool destinationStable = true;
        for (std::uint32_t state = 0; state < output.states.size(); ++state) {
            if (!matching_objects(output, index, object, state, cancel, matches)) {
                return false;
            }
            if (matches != 1) {
                destinationStable = false;
                break;
            }
        }
        if (destinationStable) {
            object.safety = catalog::GroupSafety::destinationSafe;
            continue;
        }
        const std::uint32_t bubbleRow = output.states[object.stateRow].bubbleRow;
        const catalog::Bubble& bubble = output.bubbles[bubbleRow];
        bool bubbleStable = true;
        for (std::uint32_t offset = 0; offset < bubble.stateCount; ++offset) {
            if (!matching_objects(
                    output, index, object, bubble.firstState + offset, cancel, matches)) {
                return false;
            }
            if (matches != 1) {
                bubbleStable = false;
                break;
            }
        }
        object.safety =
            bubbleStable ? catalog::GroupSafety::bubbleSafe : catalog::GroupSafety::stateOnly;
    }
    return true;
}

} // namespace sunrise::client::content::activity::scriptables::internal
