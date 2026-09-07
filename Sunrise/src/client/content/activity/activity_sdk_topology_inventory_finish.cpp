#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "activity_sdk_topology_inventory_internal.h"

namespace sunrise::client::content::activity::sdk_generation::topology_inventory {

namespace catalog = state::build_data::scriptables;
namespace format = state::activity_sdk::format;

namespace detail {

/** Checks mutable prefix state before another shard is linked. */
bool valid_open_prefix(const Snapshot& output) noexcept {
    if (output.ready || output.nextScenario > output.scenarios.size() || !output.slots.empty()
        || !output.inlineNames.empty() || !output.inlineNameBytes.empty()
        || !output.observedAliases.empty() || !output.strings.empty()
        || output.nameInventoryComplete || output.stringInventoryComplete) {
        return false;
    }
    if (output.accumulator.objectRows.size() != output.objects.size()
        || output.accumulator.inlineNames.size() > format::kAbsentIndex
        || output.accumulator.inlineNameBytes > format::kAbsentIndex) {
        return false;
    }
    std::size_t nextBubble = 0;
    std::size_t nextState = 0;
    std::size_t nextOccurrence = 0;
    for (std::size_t index = 0; index < output.scenarios.size(); ++index) {
        const Scenario& scenario = output.scenarios[index];
        if (index < output.nextScenario) {
            const std::size_t expectedBubble = scenario.bubbleCount == 0 ? 0 : nextBubble;
            const std::size_t expectedState = scenario.stateCount == 0 ? 0 : nextState;
            const std::size_t expectedOccurrence =
                scenario.occurrenceCount == 0 ? 0 : nextOccurrence;
            if (scenario.firstBubble != expectedBubble || scenario.firstState != expectedState
                || scenario.firstOccurrence != expectedOccurrence
                || scenario.bubbleCount > output.bubbles.size() - nextBubble
                || scenario.stateCount > output.states.size() - nextState
                || scenario.occurrenceCount > output.occurrences.size() - nextOccurrence) {
                return false;
            }
            nextBubble += scenario.bubbleCount;
            nextState += scenario.stateCount;
            nextOccurrence += scenario.occurrenceCount;
        } else if (scenario.firstBubble != 0 || scenario.bubbleCount != 0
                   || scenario.firstState != 0 || scenario.stateCount != 0
                   || scenario.firstOccurrence != 0 || scenario.occurrenceCount != 0) {
            return false;
        }
    }
    if (nextBubble != output.bubbles.size() || nextState != output.states.size()
        || nextOccurrence != output.occurrences.size()) {
        return false;
    }
    return true;
}

} // namespace detail
namespace {

using detail::byte_less;
using detail::kActivityTopologyJoinMask;
using detail::kActivityTopologyReadyMask;
using detail::text_view;
using detail::valid_open_prefix;
using detail::valid_registry_field;
using detail::valid_utf8;

/** One owned inline value paired with its package hash for final ordering. */
struct InlineValue final {
    std::uint32_t hash{};
    const std::string* value{};
};

/** Returns the compact FNV-1 hash used by package-inline names. */
[[nodiscard]] std::uint32_t content_hash(std::string_view value) noexcept {
    std::uint32_t hash = 2166136261U;
    for (const unsigned char byte : value) {
        hash = (hash * 16777619U) ^ byte;
    }
    return hash;
}

/** Builds the sorted unique inline bank once after every scenario was read. */
[[nodiscard]] bool build_inline_bank(const Snapshot& source,
                                     std::vector<InlineName>& rows,
                                     std::vector<std::byte>& bytes) {
    if (source.accumulator.inlineNames.size() > format::kAbsentIndex
        || source.accumulator.inlineNameBytes > format::kAbsentIndex) {
        return false;
    }
    std::vector<InlineValue> values{};
    values.reserve(source.accumulator.inlineNames.size());
    for (const std::string& value : source.accumulator.inlineNames) {
        values.push_back({content_hash(value), &value});
    }
    std::sort(
        values.begin(), values.end(), [](const InlineValue& first, const InlineValue& second) {
            return first.hash != second.hash ? first.hash < second.hash
                                             : byte_less(*first.value, *second.value);
        });
    rows.clear();
    bytes.clear();
    rows.reserve(values.size());
    bytes.reserve(source.accumulator.inlineNameBytes);
    for (const InlineValue& sourceValue : values) {
        const std::string& value = *sourceValue.value;
        rows.push_back({sourceValue.hash,
                        static_cast<std::uint32_t>(bytes.size()),
                        static_cast<std::uint32_t>(value.size())});
        for (const unsigned char byte : value) {
            bytes.push_back(static_cast<std::byte>(byte));
        }
    }
    return true;
}

/** Checks canonical object evidence once before final row linking. */
[[nodiscard]] bool valid_objects(const std::vector<Object>& objects) noexcept {
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const Object& object = objects[index];
        if (object.objectTag == 0 || object.objectTag == format::kAbsentIndex
            || (object.countEvidenceComplete && object.pendingCountMask != 0)
            || (!object.countEvidenceComplete && object.pendingCountMask != kObjectCountPendingMask)
            || (object.placedSubblockCount == 0 && object.placedLeafCount != 0)
            || (object.placedLeafCount == 0
                && (object.placedHopCount != 0 || object.configCount != 0
                    || object.bareTargetCount != 0))
            || object.configCount > object.placedHopCount
            || object.bareTargetCount > object.placedHopCount
            || object.definitionSlots.size() != object.slotCount
            || (index != 0 && objects[index - 1].objectTag >= object.objectTag)) {
            return false;
        }
        std::uint64_t descriptorCount = 0;
        for (std::size_t ordinal = 0; ordinal < object.definitionSlots.size(); ++ordinal) {
            const Slot& slot = object.definitionSlots[ordinal];
            if (slot.slotIndex != ordinal || slot.componentClass != format::kAbsentIndex
                || slot.senseSchema != format::kAbsentIndex
                || slot.authSchema != format::kAbsentIndex || slot.flags != 0) {
                return false;
            }
            if (object.descriptorEvidenceComplete) {
                if (slot.descriptorCount == format::kAbsentIndex
                    || slot.descriptorEvidence.size() != slot.descriptorCount) {
                    return false;
                }
                descriptorCount += slot.descriptorCount;
            } else if (slot.descriptorCount != format::kAbsentIndex
                       || !slot.descriptorEvidence.empty()
                       || slot.descriptorComponentClass != format::kAbsentIndex
                       || slot.descriptorSenseSchema != format::kAbsentIndex
                       || slot.descriptorAuthSchema != format::kAbsentIndex) {
                return false;
            }
        }
        if ((object.descriptorEvidenceComplete
             && (descriptorCount != object.descriptorCount
                 || descriptorCount >= format::kAbsentIndex))
            || (!object.descriptorEvidenceComplete
                && object.descriptorCount != format::kAbsentIndex)) {
            return false;
        }
    }
    return true;
}

/** Checks that every child range closes in global scenario order. */
[[nodiscard]] bool valid_ranges(const Snapshot& output) noexcept {
    std::size_t nextBubble = 0;
    std::size_t nextState = 0;
    std::size_t nextOccurrence = 0;
    for (const Scenario& scenario : output.scenarios) {
        const std::size_t expectedBubble = scenario.bubbleCount == 0 ? 0 : nextBubble;
        const std::size_t expectedState = scenario.stateCount == 0 ? 0 : nextState;
        const std::size_t expectedOccurrence = scenario.occurrenceCount == 0 ? 0 : nextOccurrence;
        if (scenario.firstBubble != expectedBubble || scenario.firstState != expectedState
            || scenario.firstOccurrence != expectedOccurrence || nextBubble > output.bubbles.size()
            || nextState > output.states.size() || nextOccurrence > output.occurrences.size()
            || scenario.bubbleCount > output.bubbles.size() - nextBubble
            || scenario.stateCount > output.states.size() - nextState
            || scenario.occurrenceCount > output.occurrences.size() - nextOccurrence) {
            return false;
        }
        nextBubble += scenario.bubbleCount;
        nextState += scenario.stateCount;
        nextOccurrence += scenario.occurrenceCount;
    }
    if (nextBubble != output.bubbles.size() || nextState != output.states.size()
        || nextOccurrence != output.occurrences.size()) {
        return false;
    }
    nextState = 0;
    for (const Bubble& bubble : output.bubbles) {
        const std::size_t expectedFirst = bubble.stateCount == 0 ? 0 : nextState;
        if (bubble.firstState != expectedFirst || nextState > output.states.size()
            || bubble.stateCount > output.states.size() - nextState) {
            return false;
        }
        nextState += bubble.stateCount;
    }
    return nextState == output.states.size();
}

/** Orders occurrences exactly as the v9 builder does after global indices are linked. */
[[nodiscard]] bool occurrence_less(const Occurrence& first, const Occurrence& second) noexcept {
    if (first.scenarioIndex != second.scenarioIndex) {
        return first.scenarioIndex < second.scenarioIndex;
    }
    if (first.bubbleIndex != second.bubbleIndex) {
        return first.bubbleIndex < second.bubbleIndex;
    }
    if (first.stateIndex != second.stateIndex) {
        return first.stateIndex < second.stateIndex;
    }
    std::string_view firstId{};
    std::string_view secondId{};
    if (!text_view(first.id, firstId) || !text_view(second.id, secondId)) {
        return false;
    }
    return byte_less(firstId, secondId);
}

/** Appends observed values for one hash without publishing v9 aliases. */
[[nodiscard]] bool resolve_observed(std::uint32_t hash,
                                    const std::vector<InlineName>& names,
                                    std::vector<std::uint32_t>& aliases,
                                    std::uint32_t& selected,
                                    std::uint32_t& firstAlias,
                                    std::uint32_t& aliasCount) {
    selected = catalog::kNoRow;
    firstAlias = static_cast<std::uint32_t>(aliases.size());
    aliasCount = 0;
    const auto first = std::lower_bound(
        names.begin(), names.end(), hash, [](const InlineName& row, std::uint32_t value) {
            return row.hash < value;
        });
    const auto last =
        std::upper_bound(first, names.end(), hash, [](std::uint32_t value, const InlineName& row) {
            return value < row.hash;
        });
    const std::size_t count = static_cast<std::size_t>(last - first);
    if (aliases.size() >= format::kAbsentIndex || count >= format::kAbsentIndex
        || count > format::kAbsentIndex - aliases.size()) {
        return false;
    }
    aliasCount = static_cast<std::uint32_t>(count);
    for (auto current = first; current != last; ++current) {
        aliases.push_back(static_cast<std::uint32_t>(current - names.begin()));
    }
    if (count == 1) {
        selected = static_cast<std::uint32_t>(first - names.begin());
    }
    return true;
}

/** Adds one nonempty valid UTF-8 value to the proved partial string inventory. */
[[nodiscard]] bool add_string(const Text& value, std::vector<std::string>& output) {
    std::string_view view{};
    if (!text_view(value, view) || !valid_utf8(view)) {
        return false;
    }
    if (!view.empty()) {
        output.emplace_back(view);
    }
    return true;
}

/** Finds one exact value in the sorted partial string inventory. */
[[nodiscard]] bool string_index(const std::vector<std::string>& strings,
                                const Text& value,
                                std::uint32_t& output) noexcept {
    output = catalog::kNoRow;
    std::string_view view{};
    if (!text_view(value, view) || view.empty()) {
        return false;
    }
    const auto found = std::lower_bound(
        strings.begin(), strings.end(), view, [](const std::string& row, std::string_view target) {
            return byte_less(row, target);
        });
    if (found == strings.end() || std::string_view(*found) != view) {
        return false;
    }
    output = static_cast<std::uint32_t>(found - strings.begin());
    return true;
}

/** Builds value-owned strings while leaving final v9 byte offsets unresolved. */
[[nodiscard]] bool build_strings(const Snapshot& source,
                                 std::vector<Object>& objects,
                                 std::vector<Occurrence>& occurrences,
                                 std::vector<Slot>& slots,
                                 std::vector<std::string>& output) {
    output.clear();
    for (const Activity& row : source.activities) {
        if (!add_string(row.id, output) || !add_string(row.internalName, output)
            || !add_string(row.displayName, output)) {
            return false;
        }
    }
    for (const Scenario& row : source.scenarios) {
        if (!add_string(row.id, output) || !add_string(row.name, output)) {
            return false;
        }
    }
    for (const Bubble& row : source.bubbles) {
        if (!add_string(row.id, output)) {
            return false;
        }
    }
    for (const State& row : source.states) {
        if (!add_string(row.id, output) || !add_string(row.entryId, output)
            || !add_string(row.registryId, output)) {
            return false;
        }
    }
    for (const Object& row : objects) {
        if (!add_string(row.id, output)) {
            return false;
        }
    }
    for (const Occurrence& row : occurrences) {
        if (!add_string(row.id, output) || !add_string(row.contextRegistryKey, output)
            || !add_string(row.registryId, output) || !add_string(row.entryId, output)) {
            return false;
        }
    }
    for (const Slot& row : slots) {
        if (!add_string(row.id, output)) {
            return false;
        }
    }
    std::sort(
        output.begin(), output.end(), [](const std::string& first, const std::string& second) {
            return byte_less(first, second);
        });
    output.erase(std::unique(output.begin(), output.end()), output.end());
    if (output.size() >= format::kAbsentIndex) {
        return false;
    }
    for (Object& row : objects) {
        if (!string_index(output, row.id, row.idString)) {
            return false;
        }
    }
    for (Occurrence& row : occurrences) {
        if (!string_index(output, row.id, row.idString)
            || !string_index(output, row.contextRegistryKey, row.contextRegistryKeyString)
            || !string_index(output, row.registryId, row.registryIdString)
            || !string_index(output, row.entryId, row.entryIdString)) {
            return false;
        }
    }
    for (Slot& row : slots) {
        if (!string_index(output, row.id, row.idString)) {
            return false;
        }
    }
    return true;
}

} // namespace

/** Closes structural rows with exact object counts while names and content remain pending. */
bool finish(Snapshot& output) noexcept {
    if (!valid_open_prefix(output) || output.nextScenario != output.scenarios.size()
        || !valid_ranges(output)) {
        return false;
    }
    try {
        std::vector<Object> objects = output.objects;
        std::sort(objects.begin(), objects.end(), [](const Object& first, const Object& second) {
            return first.objectTag < second.objectTag;
        });
        if (!valid_objects(objects)) {
            return false;
        }
        std::vector<Occurrence> occurrences = output.occurrences;
        std::vector<Slot> slots{};
        std::vector<Bubble> bubbles = output.bubbles;
        std::vector<InlineName> inlineNames{};
        std::vector<std::byte> inlineNameBytes{};
        std::vector<std::uint32_t> observedAliases{};
        std::vector<std::string> strings{};
        if (!build_inline_bank(output, inlineNames, inlineNameBytes)) {
            return false;
        }

        for (std::size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
            Object& object = objects[objectIndex];
            if (!object.countEvidenceComplete || object.pendingCountMask != 0
                || object.definitionSlots.size() != object.slotCount
                || slots.size() >= format::kAbsentIndex
                || object.slotCount > format::kAbsentIndex - slots.size()) {
                return false;
            }
            object.firstSlot = static_cast<std::uint32_t>(slots.size());
            for (Slot slot : object.definitionSlots) {
                slot.objectIndex = static_cast<std::uint32_t>(objectIndex);
                slots.push_back(std::move(slot));
            }
        }

        std::sort(occurrences.begin(), occurrences.end(), occurrence_less);
        for (std::size_t index = 0; index < occurrences.size(); ++index) {
            Occurrence& occurrence = occurrences[index];
            if (occurrence.scenarioIndex >= output.scenarios.size()
                || occurrence.bubbleIndex >= output.bubbles.size()
                || occurrence.stateIndex >= output.states.size()
                || !valid_registry_field(occurrence.registryField)) {
                return false;
            }
            const State& state = output.states[occurrence.stateIndex];
            if (state.scenarioIndex != occurrence.scenarioIndex
                || state.bubbleIndex != occurrence.bubbleIndex
                || state.registryTag != occurrence.registryTag
                || state.entryTag != occurrence.entryTag) {
                return false;
            }
            const auto object =
                std::lower_bound(objects.begin(),
                                 objects.end(),
                                 occurrence.objectTag,
                                 [](const Object& row, auto tag) { return row.objectTag < tag; });
            if (object == objects.end() || object->objectTag != occurrence.objectTag) {
                return false;
            }
            occurrence.objectIndex = static_cast<std::uint32_t>(object - objects.begin());
            if (index != 0) {
                const Occurrence& previous = occurrences[index - 1];
                std::string_view previousId{};
                std::string_view currentId{};
                if (!text_view(previous.id, previousId) || !text_view(occurrence.id, currentId)
                    || previousId == currentId
                    || (previous.scenarioIndex == occurrence.scenarioIndex
                        && previous.stateIndex == occurrence.stateIndex
                        && previous.registryField == occurrence.registryField
                        && previous.objectOrdinal == occurrence.objectOrdinal)) {
                    return false;
                }
            }
        }

        std::size_t nextOccurrence = 0;
        for (std::size_t scenarioIndex = 0; scenarioIndex < output.scenarios.size();
             ++scenarioIndex) {
            const Scenario& scenario = output.scenarios[scenarioIndex];
            const std::size_t expectedFirst = scenario.occurrenceCount == 0 ? 0 : nextOccurrence;
            if (scenario.firstOccurrence != expectedFirst
                || scenario.occurrenceCount > occurrences.size() - nextOccurrence) {
                return false;
            }
            for (std::size_t row = nextOccurrence; row < nextOccurrence + scenario.occurrenceCount;
                 ++row) {
                if (occurrences[row].scenarioIndex != scenarioIndex) {
                    return false;
                }
            }
            nextOccurrence += scenario.occurrenceCount;
        }
        if (nextOccurrence != occurrences.size()) {
            return false;
        }

        for (Bubble& bubble : bubbles) {
            if (!resolve_observed(bubble.nameHash,
                                  inlineNames,
                                  observedAliases,
                                  bubble.observedNameRow,
                                  bubble.firstObservedAlias,
                                  bubble.observedAliasCount)) {
                return false;
            }
        }
        for (Slot& slot : slots) {
            if (!resolve_observed(slot.nameHash,
                                  inlineNames,
                                  observedAliases,
                                  slot.observedNameRow,
                                  slot.firstObservedAlias,
                                  slot.observedAliasCount)) {
                return false;
            }
        }
        if (!build_strings(output, objects, occurrences, slots, strings)) {
            return false;
        }

        for (const Activity& activity : output.activities) {
            if (activity.scenarioIndex != catalog::kNoRow) {
                if (activity.scenarioIndex >= output.scenarios.size()
                    || activity.exactFlags != kActivityTopologyJoinMask) {
                    return false;
                }
            } else if (activity.exactFlags != 0) {
                return false;
            }
        }

        output.objects = std::move(objects);
        output.occurrences = std::move(occurrences);
        output.slots = std::move(slots);
        output.bubbles = std::move(bubbles);
        output.inlineNames = std::move(inlineNames);
        output.inlineNameBytes = std::move(inlineNameBytes);
        output.observedAliases = std::move(observedAliases);
        output.strings = std::move(strings);
        output.accumulator = {};
        for (Activity& activity : output.activities) {
            if (activity.scenarioIndex != catalog::kNoRow) {
                activity.exactFlags = kActivityTopologyReadyMask;
            }
        }
        output.nameInventoryComplete = false;
        output.stringInventoryComplete = false;
        output.ready = true;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace sunrise::client::content::activity::sdk_generation::topology_inventory
