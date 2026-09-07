#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../../middleware/content/packages/tables/scenario_reader.h"
#include "../../../state/activity_sdk/format.h"
#include "activity_sdk_topology_inventory_internal.h"

namespace sunrise::client::content::activity::sdk_generation::topology_inventory {
using detail::build_object;
using detail::build_occurrence;
using detail::format_text;
using detail::kActivityTopologyJoinMask;
using detail::merge_object;
using detail::valid_open_prefix;
using detail::valid_registry_field;

namespace {

namespace catalog = state::build_data::scriptables;
namespace format = state::activity_sdk::format;
namespace tables = middleware::content::packages::tables;

/** One prior object replacement prepared before the scenario commits. */
struct ObjectUpdate final {
    std::size_t row{};
    Object value{};
};

/** Copies one bounded byte string and always retains a trailing zero. */
[[nodiscard]] bool copy_text(std::string_view source, Text& output) noexcept {
    output = {};
    if (source.size() >= output.value.size()
        || source.size() > (std::numeric_limits<std::uint16_t>::max)()) {
        return false;
    }
    std::copy(source.begin(), source.end(), output.value.begin());
    output.length = static_cast<std::uint16_t>(source.size());
    return true;
}

/** Returns a view only when the owned length remains inside its fixed storage. */
[[nodiscard]] bool text_view_impl(const Text& source, std::string_view& output) noexcept {
    output = {};
    if (source.length >= source.value.size()) {
        return false;
    }
    output = std::string_view(source.value.data(), source.length);
    return true;
}

/** Uses unsigned UTF-8 bytes, which is the canonical string order. */
[[nodiscard]] bool byte_less_impl(std::string_view left, std::string_view right) noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(), [](char a, char b) noexcept {
            return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
        });
}

/** Rejects malformed UTF-8 and embedded zero bytes before string storage. */
[[nodiscard]] bool valid_utf8_impl(std::string_view value) noexcept {
    std::size_t index = 0;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first == 0) {
            return false;
        }
        if (first <= 0x7FU) {
            ++index;
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU) {
            if (index + 1 >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[index + 1]);
            if (second < 0x80U || second > 0xBFU) {
                return false;
            }
            index += 2;
            continue;
        }
        if (first >= 0xE0U && first <= 0xEFU) {
            if (index + 2 >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            const bool secondValid = first == 0xE0U   ? second >= 0xA0U && second <= 0xBFU
                                     : first == 0xEDU ? second >= 0x80U && second <= 0x9FU
                                                      : second >= 0x80U && second <= 0xBFU;
            if (!secondValid || third < 0x80U || third > 0xBFU) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first >= 0xF0U && first <= 0xF4U) {
            if (index + 3 >= value.size()) {
                return false;
            }
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            const auto fourth = static_cast<unsigned char>(value[index + 3]);
            const bool secondValid = first == 0xF0U   ? second >= 0x90U && second <= 0xBFU
                                     : first == 0xF4U ? second >= 0x80U && second <= 0x8FU
                                                      : second >= 0x80U && second <= 0xBFU;
            if (!secondValid || third < 0x80U || third > 0xBFU || fourth < 0x80U
                || fourth > 0xBFU) {
                return false;
            }
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

/** Returns the compact FNV-1 hash used by package-inline names. */
[[nodiscard]] std::uint32_t content_hash(std::string_view value) noexcept {
    std::uint32_t hash = 2166136261U;
    for (const unsigned char byte : value) {
        hash = (hash * 16777619U) ^ byte;
    }
    return hash;
}

/** Finds one scenario index in the inventory's strict tag order. */
[[nodiscard]] std::uint32_t scenario_index(const Snapshot& output, std::uint32_t tag) noexcept {
    const auto found = std::lower_bound(
        output.scenarios.begin(), output.scenarios.end(), tag, [](const Scenario& row, auto value) {
            return row.tag < value;
        });
    return found != output.scenarios.end() && found->tag == tag
               ? static_cast<std::uint32_t>(found - output.scenarios.begin())
               : catalog::kNoRow;
}

/** Returns one exact value from an inline-name byte bank. */
template <typename Row>
[[nodiscard]] bool inline_value(const Row& row,
                                const std::vector<std::byte>& bytes,
                                std::string_view& output) noexcept {
    output = {};
    const std::size_t first = row.firstByte;
    if (first > bytes.size() || row.byteCount > bytes.size() - first) {
        return false;
    }
    output = std::string_view(reinterpret_cast<const char*>(bytes.data() + first), row.byteCount);
    return true;
}

/** Compares two inline-name identities by hash and unsigned UTF-8 bytes. */
template <typename LeftRow, typename RightRow>
[[nodiscard]] int compare_inline(const LeftRow& left,
                                 const std::vector<std::byte>& leftBytes,
                                 const RightRow& right,
                                 const std::vector<std::byte>& rightBytes) noexcept {
    if (left.hash != right.hash) {
        return left.hash < right.hash ? -1 : 1;
    }
    std::string_view leftValue{};
    std::string_view rightValue{};
    if (!inline_value(left, leftBytes, leftValue) || !inline_value(right, rightBytes, rightValue)) {
        return 2;
    }
    if (leftValue == rightValue) {
        return 0;
    }
    return byte_less_impl(leftValue, rightValue) ? -1 : 1;
}

/** Checks one sorted, unique, contiguous package-inline bank. */
template <typename Row>
[[nodiscard]] bool valid_inline_bank_impl(const std::vector<Row>& rows,
                                          const std::vector<std::byte>& bytes) noexcept {
    std::size_t nextByte = 0;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const Row& row = rows[index];
        std::string_view value{};
        if (row.firstByte != nextByte || row.byteCount == 0
            || row.byteCount > catalog::kInlineNameMaximumBytes || !inline_value(row, bytes, value)
            || !valid_utf8_impl(value) || content_hash(value) != row.hash) {
            return false;
        }
        if (index != 0 && compare_inline(rows[index - 1], bytes, row, bytes) >= 0) {
            return false;
        }
        nextByte += row.byteCount;
    }
    return nextByte == bytes.size();
}

/** Collects only new inline values while the final bank order remains open. */
[[nodiscard]] bool collect_inline_names(const catalog::Snapshot& source,
                                        const Snapshot& current,
                                        std::vector<std::string>& output,
                                        std::size_t& addedBytes) {
    output.clear();
    addedBytes = 0;
    if (!valid_inline_bank_impl(source.inlineNameCandidates, source.inlineNameBytes)
        || current.accumulator.inlineNames.size() > format::kAbsentIndex
        || current.accumulator.inlineNameBytes > format::kAbsentIndex) {
        return false;
    }
    output.reserve(source.inlineNameCandidates.size());
    for (const catalog::InlineNameCandidate& row : source.inlineNameCandidates) {
        std::string_view value{};
        if (!inline_value(row, source.inlineNameBytes, value)) {
            return false;
        }
        std::string owned(value);
        if (current.accumulator.inlineNames.contains(owned)) {
            continue;
        }
        if (output.size() >= format::kAbsentIndex - current.accumulator.inlineNames.size()
            || addedBytes > format::kAbsentIndex - current.accumulator.inlineNameBytes
            || value.size()
                   > format::kAbsentIndex - current.accumulator.inlineNameBytes - addedBytes) {
            return false;
        }
        addedBytes += value.size();
        output.push_back(std::move(owned));
    }
    return true;
}

/** Validates one source state and copies every v9 topology scalar. */
[[nodiscard]] bool append_state(const catalog::State& input,
                                std::uint32_t scenarioTag,
                                std::uint32_t scenarioIndex,
                                std::uint32_t bubbleIndex,
                                std::uint32_t bubbleOrdinal,
                                std::uint32_t stateOrdinal,
                                std::vector<State>& output) {
    if (!input.resolved || input.bubbleRow != bubbleOrdinal || input.index != stateOrdinal
        || input.entryTag == 0 || input.entryTag == format::kAbsentIndex || input.registryTag == 0
        || input.registryTag == format::kAbsentIndex
        || input.sliceSetIndex % tables::kSliceSetIndexFactor != 0) {
        return false;
    }
    const std::uint32_t entryIndex = input.sliceSetIndex / tables::kSliceSetIndexFactor;
    if (entryIndex != bubbleOrdinal || output.size() >= format::kAbsentIndex) {
        return false;
    }
    State state{};
    state.scenarioIndex = scenarioIndex;
    state.bubbleIndex = bubbleIndex;
    state.stateOrdinal = stateOrdinal;
    state.entryIndex = entryIndex;
    state.sliceSetIndex = input.sliceSetIndex;
    state.mapBubbleIndex = input.mapBubbleIndex;
    state.stateHash = input.stateHash;
    state.rawU32At12 = input.rawU32At12;
    state.entryTag = input.entryTag;
    state.registryTag = input.registryTag;
    state.enabled = input.enabled;
    if (!format_text(state.id,
                     "state/%08x/%04x/%04x/%08x",
                     static_cast<unsigned>(scenarioTag),
                     static_cast<unsigned>(bubbleOrdinal),
                     static_cast<unsigned>(stateOrdinal),
                     static_cast<unsigned>(input.entryTag))
        || !format_text(state.entryId, "entry/%08x", static_cast<unsigned>(input.entryTag))
        || !format_text(
            state.registryId, "registry/%08x", static_cast<unsigned>(input.registryTag))) {
        return false;
    }
    output.push_back(state);
    return true;
}

} // namespace

namespace detail {

/** Returns a view only when the owned length remains inside its fixed storage. */
bool text_view(const Text& source, std::string_view& output) noexcept {
    return text_view_impl(source, output);
}

/** Uses unsigned UTF-8 bytes, which is the canonical string order. */
bool byte_less(std::string_view left, std::string_view right) noexcept {
    return byte_less_impl(left, right);
}

/** Rejects malformed UTF-8 and embedded zero bytes before string storage. */
bool valid_utf8(std::string_view value) noexcept {
    return valid_utf8_impl(value);
}

} // namespace detail

/** Starts one deterministic topology build from the complete activity inventory. */
bool begin(const activity_inventory::Snapshot& source, Snapshot& output) noexcept {
    output = {};
    if (!activity_inventory::validate(source) || source.scenarios.empty()
        || source.scenarios.size() >= format::kAbsentIndex
        || source.activities.size() >= format::kAbsentIndex) {
        return false;
    }
    try {
        Snapshot pending{};
        pending.scenarios.reserve(source.scenarios.size());
        for (const activity_inventory::ScenarioRoot& sourceScenario : source.scenarios) {
            Scenario row{};
            row.tag = sourceScenario.tag;
            if (!format_text(row.id, "scenario/%08x", static_cast<unsigned>(sourceScenario.tag))
                || !copy_text(
                    std::string_view(sourceScenario.name.data(), sourceScenario.nameLength),
                    row.name)) {
                return false;
            }
            pending.scenarios.push_back(row);
        }
        pending.activities.reserve(source.activities.size());
        for (const activity_inventory::ActivityVariant& sourceActivity : source.activities) {
            Activity row{};
            row.activityIndex = sourceActivity.definition.activityIndex;
            row.definitionHash = sourceActivity.definition.definitionHash;
            if (!format_text(row.id,
                             "act/%04x/%08x",
                             static_cast<unsigned>(row.activityIndex),
                             static_cast<unsigned>(row.definitionHash))
                || !copy_text(std::string_view(sourceActivity.definition.internalName.data(),
                                               sourceActivity.definition.internalNameLength),
                              row.internalName)) {
                return false;
            }
            if (sourceActivity.joinStatus == activity_inventory::JoinStatus::exact) {
                row.scenarioIndex = scenario_index(pending, sourceActivity.scenarioTag);
                if (row.scenarioIndex == catalog::kNoRow) {
                    return false;
                }
                row.exactFlags = kActivityTopologyJoinMask;
            }
            pending.activities.push_back(row);
        }
        output = std::move(pending);
        return true;
    } catch (...) {
        output = {};
        return false;
    }
}

/** Appends the next scenario shard and rejects gaps, repeats, and unsafe source rows. */
bool append(const catalog::Snapshot& source, Snapshot& output) noexcept {
    if (!valid_open_prefix(output) || output.nextScenario >= output.scenarios.size()
        || source.status != catalog::BuildStatus::ready
        || source.coverage != catalog::BuildCoverage::full) {
        return false;
    }
    const Scenario& scenario = output.scenarios[output.nextScenario];
    if (source.scenarioTag != scenario.tag || source.scenarioNameLength != scenario.name.length
        || !std::equal(source.scenarioName.begin(),
                       source.scenarioName.begin() + source.scenarioNameLength,
                       scenario.name.value.begin())
        || source.bubbles.size() >= format::kAbsentIndex
        || source.states.size() >= format::kAbsentIndex
        || source.objects.size() >= format::kAbsentIndex
        || output.bubbles.size() > format::kAbsentIndex - source.bubbles.size()
        || output.states.size() > format::kAbsentIndex - source.states.size()
        || output.occurrences.size() > format::kAbsentIndex - source.objects.size()) {
        return false;
    }
    try {
        const std::size_t firstBubble = output.bubbles.size();
        const std::size_t firstState = output.states.size();
        const std::size_t firstOccurrence = output.occurrences.size();
        std::vector<Bubble> pendingBubbles{};
        std::vector<State> pendingStates{};
        std::vector<Occurrence> pendingOccurrences{};
        std::vector<Object> pendingObjects{};
        std::vector<std::string> pendingInlineNames{};
        std::size_t pendingInlineNameBytes = 0;
        if (!collect_inline_names(source, output, pendingInlineNames, pendingInlineNameBytes)) {
            return false;
        }
        pendingBubbles.reserve(source.bubbles.size());
        pendingStates.reserve(source.states.size());
        pendingOccurrences.reserve(source.objects.size());
        std::size_t localState = 0;
        for (std::size_t ordinal = 0; ordinal < source.bubbles.size(); ++ordinal) {
            const catalog::Bubble& input = source.bubbles[ordinal];
            if (input.index != ordinal || input.firstState != localState
                || localState > source.states.size()
                || input.stateCount > source.states.size() - localState) {
                return false;
            }
            Bubble bubble{};
            bubble.scenarioIndex = static_cast<std::uint32_t>(output.nextScenario);
            bubble.bubbleOrdinal = static_cast<std::uint32_t>(ordinal);
            bubble.nameHash = input.nameHash;
            bubble.firstState = input.stateCount == 0
                                    ? 0
                                    : static_cast<std::uint32_t>(firstState + pendingStates.size());
            bubble.stateCount = input.stateCount;
            if (!format_text(bubble.id,
                             "bubble/%08x/%04x/%08x",
                             static_cast<unsigned>(source.scenarioTag),
                             static_cast<unsigned>(ordinal),
                             static_cast<unsigned>(input.nameHash))) {
                return false;
            }
            const std::uint32_t bubbleIndex =
                static_cast<std::uint32_t>(firstBubble + pendingBubbles.size());
            pendingBubbles.push_back(bubble);
            for (std::uint32_t stateOrdinal = 0; stateOrdinal < input.stateCount; ++stateOrdinal) {
                if (!append_state(source.states[localState + stateOrdinal],
                                  source.scenarioTag,
                                  bubble.scenarioIndex,
                                  bubbleIndex,
                                  bubble.bubbleOrdinal,
                                  stateOrdinal,
                                  pendingStates)) {
                    return false;
                }
            }
            localState += input.stateCount;
        }
        if (localState != source.states.size()) {
            return false;
        }

        std::size_t nextSlot = 0;
        std::size_t nextDescriptor = 0;
        bool havePreviousOccurrence = false;
        std::tuple<std::uint32_t, std::uint32_t, std::uint32_t> previousGroup{};
        std::uint32_t previousOrdinal{};
        for (std::size_t objectRow = 0; objectRow < source.objects.size(); ++objectRow) {
            const catalog::Object& input = source.objects[objectRow];
            if (input.bubbleRow >= source.bubbles.size() || input.stateRow >= source.states.size()
                || source.states[input.stateRow].bubbleRow != input.bubbleRow
                || input.registryTag != source.states[input.stateRow].registryTag
                || !valid_registry_field(input.registryDescriptor)
                || input.objectIndex == catalog::kNoRow) {
                return false;
            }
            const auto group = std::tuple(input.bubbleRow,
                                          input.stateRow,
                                          static_cast<std::uint32_t>(input.registryDescriptor));
            if (!havePreviousOccurrence || group != previousGroup) {
                if ((havePreviousOccurrence && !(previousGroup < group))
                    || input.objectIndex != 0) {
                    return false;
                }
            } else if (previousOrdinal == (std::numeric_limits<std::uint32_t>::max)()
                       || input.objectIndex != previousOrdinal + 1U) {
                return false;
            }
            previousGroup = group;
            previousOrdinal = input.objectIndex;
            havePreviousOccurrence = true;

            Object definition{};
            if (!build_object(source,
                              static_cast<std::uint32_t>(objectRow),
                              nextSlot,
                              nextDescriptor,
                              definition)
                || !merge_object(std::move(definition), pendingObjects)) {
                return false;
            }
            const std::uint32_t bubbleIndex =
                static_cast<std::uint32_t>(firstBubble + input.bubbleRow);
            const std::uint32_t stateIndex =
                static_cast<std::uint32_t>(firstState + input.stateRow);
            Occurrence occurrence{};
            if (!build_occurrence(input,
                                  pendingStates[input.stateRow],
                                  source.scenarioTag,
                                  static_cast<std::uint32_t>(output.nextScenario),
                                  bubbleIndex,
                                  stateIndex,
                                  occurrence)) {
                return false;
            }
            pendingOccurrences.push_back(occurrence);
        }
        if (nextSlot != source.slots.size() || nextDescriptor != source.descriptors.size()) {
            return false;
        }

        std::vector<ObjectUpdate> objectUpdates{};
        std::vector<Object> newObjects{};
        objectUpdates.reserve(pendingObjects.size());
        newObjects.reserve(pendingObjects.size());
        for (Object& definition : pendingObjects) {
            const auto found = output.accumulator.objectRows.find(definition.objectTag);
            if (found == output.accumulator.objectRows.end()) {
                newObjects.push_back(std::move(definition));
                continue;
            }
            if (found->second >= output.objects.size()
                || output.objects[found->second].objectTag != definition.objectTag) {
                return false;
            }
            Object merged = output.objects[found->second];
            if (!merge_object(std::move(definition), merged)) {
                return false;
            }
            objectUpdates.push_back({found->second, std::move(merged)});
        }

        output.bubbles.reserve(firstBubble + pendingBubbles.size());
        output.states.reserve(firstState + pendingStates.size());
        output.occurrences.reserve(firstOccurrence + pendingOccurrences.size());
        output.objects.reserve(output.objects.size() + newObjects.size());
        output.accumulator.objectRows.reserve(output.objects.size() + newObjects.size());
        output.accumulator.inlineNames.reserve(output.accumulator.inlineNames.size()
                                               + pendingInlineNames.size());

        std::size_t insertedInlineNames = 0;
        std::size_t insertedObjectRows = 0;
        const auto roll_back_indexes = [&]() noexcept {
            for (std::size_t index = 0; index < insertedObjectRows; ++index) {
                output.accumulator.objectRows.erase(newObjects[index].objectTag);
            }
            for (std::size_t index = 0; index < insertedInlineNames; ++index) {
                output.accumulator.inlineNames.erase(pendingInlineNames[index]);
            }
        };
        try {
            for (const std::string& value : pendingInlineNames) {
                if (!output.accumulator.inlineNames.insert(value).second) {
                    roll_back_indexes();
                    return false;
                }
                ++insertedInlineNames;
            }
            for (std::size_t index = 0; index < newObjects.size(); ++index) {
                const std::size_t row = output.objects.size() + index;
                if (!output.accumulator.objectRows.emplace(newObjects[index].objectTag, row)
                         .second) {
                    roll_back_indexes();
                    return false;
                }
                ++insertedObjectRows;
            }
        } catch (...) {
            roll_back_indexes();
            return false;
        }

        for (ObjectUpdate& update : objectUpdates) {
            output.objects[update.row] = std::move(update.value);
        }
        for (Object& object : newObjects) {
            output.objects.push_back(std::move(object));
        }
        output.accumulator.inlineNameBytes += pendingInlineNameBytes;
        output.bubbles.insert(output.bubbles.end(), pendingBubbles.begin(), pendingBubbles.end());
        output.states.insert(output.states.end(), pendingStates.begin(), pendingStates.end());
        output.occurrences.insert(
            output.occurrences.end(), pendingOccurrences.begin(), pendingOccurrences.end());
        Scenario& committed = output.scenarios[output.nextScenario];
        committed.firstBubble =
            pendingBubbles.empty() ? 0 : static_cast<std::uint32_t>(firstBubble);
        committed.bubbleCount = static_cast<std::uint32_t>(pendingBubbles.size());
        committed.firstState = pendingStates.empty() ? 0 : static_cast<std::uint32_t>(firstState);
        committed.stateCount = static_cast<std::uint32_t>(pendingStates.size());
        committed.firstOccurrence =
            pendingOccurrences.empty() ? 0 : static_cast<std::uint32_t>(firstOccurrence);
        committed.occurrenceCount = static_cast<std::uint32_t>(pendingOccurrences.size());
        ++output.nextScenario;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace sunrise::client::content::activity::sdk_generation::topology_inventory
