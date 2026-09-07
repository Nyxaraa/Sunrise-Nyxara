#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>

#include "../../../state/build_data/scriptables/inline_name_evidence.h"
#include "activity_sdk_policy_inventory_internal.h"

namespace sunrise::client::content::activity::sdk_generation::policy_inventory::internal {
namespace {

namespace evidence = state::build_data::scriptables::inline_name_evidence;

/** Object/scenario binding keys store each format-v12 index in one 32-bit half. */
constexpr std::size_t kObjectBindingShift = 32;

/**
 * Checks one activity join outcome.
 * @param value The candidate enum value.
 * @return True when value names an exhaustive outcome.
 */
[[nodiscard]] bool valid_join_status(ActivityJoinStatus value) noexcept {
    switch (value) {
    case ActivityJoinStatus::exact:
    case ActivityJoinStatus::sourceNameMissing:
    case ActivityJoinStatus::liveNameMissing:
    case ActivityJoinStatus::liveNameAmbiguous:
        return true;
    }
    return false;
}

/**
 * Checks one slot extraction outcome.
 * @param value The candidate enum value.
 * @return True when value names a supported outcome.
 */
[[nodiscard]] bool valid_extraction_status(ExtractionStatus value) noexcept {
    switch (value) {
    case ExtractionStatus::complete:
    case ExtractionStatus::completeEmpty:
    case ExtractionStatus::partial:
        return true;
    }
    return false;
}

} // namespace

std::size_t StringHash::operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
}

std::size_t StringHash::operator()(const std::string& value) const noexcept {
    return (*this)(std::string_view(value));
}

bool StringEqual::operator()(std::string_view left, std::string_view right) const noexcept {
    return left == right;
}

bool StringEqual::operator()(const std::string& left, const std::string& right) const noexcept {
    return left == right;
}

bool StringEqual::operator()(const std::string& left, std::string_view right) const noexcept {
    return std::string_view(left) == right;
}

bool StringEqual::operator()(std::string_view left, const std::string& right) const noexcept {
    return left == std::string_view(right);
}

/**
 * Retains one nonempty failure reason within the fixed gate-set capacity.
 * @param value The optional stable reason code.
 * @return True when the value is empty or fits the collection.
 */
bool FailureReasons::add(std::string_view value) noexcept {
    if (value.empty()) {
        return true;
    }
    if (count >= values.size()) {
        return false;
    }
    values[count++] = value;
    return true;
}

void FailureReasons::canonicalize() noexcept {
    const auto used = static_cast<std::ptrdiff_t>(count);
    std::sort(values.begin(), values.begin() + used, byte_less);
    count = static_cast<std::size_t>(std::unique(values.begin(), values.begin() + used)
                                     - values.begin());
}

/**
 * Compares two values by canonical unsigned UTF-8 byte order.
 * @param left The first value.
 * @param right The second value.
 * @return True when left precedes right.
 */
bool byte_less(std::string_view left, std::string_view right) noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(), [](char a, char b) noexcept {
            return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
        });
}

/**
 * Checks one bounded row-count addition without overflow.
 * @param current The existing row count.
 * @param count The proposed added rows.
 * @param maximum The section capacity.
 * @return True when the proposed count fits.
 */
bool can_add(std::size_t current, std::size_t count, std::size_t maximum) noexcept {
    return current <= maximum && count <= maximum - current;
}

/**
 * Converts one owned interval while retaining the cursor for an empty range.
 * @param first The current child-row cursor.
 * @param count The number of owned rows.
 * @param output Receives the format-v12 range.
 * @return True when both scalars fit the packed fields.
 */
bool make_range(std::size_t first, std::size_t count, format::Range& output) noexcept {
    output = {};
    if (first > (std::numeric_limits<std::uint32_t>::max)()
        || count > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    output.first = static_cast<std::uint32_t>(first);
    output.count = static_cast<std::uint32_t>(count);
    return true;
}

/**
 * Checks one optional deferred string.
 * @param value The borrowed string.
 * @return True for empty or bounded canonical UTF-8 without embedded zero bytes.
 */
bool valid_text(std::string_view value) noexcept {
    if (value.empty()) {
        return true;
    }
    if (value.size() > kMaximumTextBytes || value.find('\0') != std::string_view::npos) {
        return false;
    }
    return evidence::valid_utf8(std::as_bytes(std::span<const char>(value.data(), value.size())));
}

/**
 * Checks parent order, identity, bounds, and every borrowed UTF-8 value.
 * @param inputs The complete final parent-row views.
 * @return True when every policy prerequisite is valid.
 */
bool valid_inputs(const Inputs& inputs) {
    if (inputs.activities.size() > kMaximumActivityCount
        || inputs.scenarioCount > kMaximumScenarioCount || inputs.objectCount > kMaximumObjectCount
        || inputs.slots.size() > kMaximumSlotCount
        || inputs.occurrences.size() > kMaximumOccurrenceCount
        || inputs.hostSurfaces.size() > kMaximumHostSurfaceCount
        || inputs.slotAliases.size() > kMaximumPolicyTextCount) {
        return false;
    }

    std::unordered_set<std::string_view> activityIds{};
    activityIds.reserve(inputs.activities.size());
    for (std::size_t index = 0; index < inputs.activities.size(); ++index) {
        const ActivityInput& row = inputs.activities[index];
        const bool sourceNameMissing = row.joinStatus == ActivityJoinStatus::sourceNameMissing;
        if (!valid_join_status(row.joinStatus) || row.activityIndex != index || row.id.empty()
            || !valid_text(row.id) || !valid_text(row.internalName) || !valid_text(row.displayName)
            || sourceNameMissing != row.internalName.empty()
            || !activityIds.insert(row.id).second) {
            return false;
        }
    }

    std::unordered_set<std::string_view> slotIds{};
    slotIds.reserve(inputs.slots.size());
    std::uint32_t priorObject = 0;
    bool havePriorObject = false;
    for (const SlotInput& row : inputs.slots) {
        if (!valid_extraction_status(row.extractionStatus) || row.objectIndex >= inputs.objectCount
            || row.id.empty() || !valid_text(row.id) || !valid_text(row.name)
            || !valid_text(row.senseSchemaId) || !valid_text(row.authSchemaId)
            || !slotIds.insert(row.id).second || (row.flags & ~format::kSlotFlagMask) != 0
            || (havePriorObject && row.objectIndex < priorObject)) {
            return false;
        }
        priorObject = row.objectIndex;
        havePriorObject = true;
    }
    for (const SlotAliasInput& row : inputs.slotAliases) {
        if (row.slotIndex >= inputs.slots.size() || !valid_text(row.value)) {
            return false;
        }
    }
    for (const OccurrenceInput& row : inputs.occurrences) {
        if (row.objectIndex >= inputs.objectCount
            || (row.scenarioIndex != format::kAbsentIndex
                && row.scenarioIndex >= inputs.scenarioCount)) {
            return false;
        }
    }

    std::unordered_set<std::string> hostCapabilityIds{};
    hostCapabilityIds.reserve(inputs.hostSurfaces.size());
    for (const HostSurface& row : inputs.hostSurfaces) {
        if (row.id.empty() || row.operation.empty() || !row.id.starts_with("host-api/")
            || !valid_text(row.id) || !valid_text(row.operation)) {
            return false;
        }
        std::string id("cap/");
        id.append(row.id);
        id.push_back('/');
        id.append(row.operation);
        if (id.size() > kMaximumTextBytes || !hostCapabilityIds.insert(std::move(id)).second) {
            return false;
        }
    }
    return true;
}

/**
 * Builds complete object route evidence without retaining occurrence strings.
 * @param inputs The validated parent rows and occurrence bindings.
 * @param output Receives one route summary for each object row.
 * @return True when every scenario-local occurrence count fits its field.
 */
bool build_routes(const Inputs& inputs, std::vector<RouteEvidence>& output) {
    output.assign(inputs.objectCount, {});
    std::vector<std::uint64_t> bindings{};
    bindings.reserve(inputs.occurrences.size());
    for (const OccurrenceInput& row : inputs.occurrences) {
        RouteEvidence& route = output[row.objectIndex];
        if (row.scenarioIndex == format::kAbsentIndex) {
            if (route.invalidOccurrences == (std::numeric_limits<std::uint32_t>::max)()) {
                return false;
            }
            ++route.invalidOccurrences;
            continue;
        }
        bindings.push_back((static_cast<std::uint64_t>(row.objectIndex) << kObjectBindingShift)
                           | row.scenarioIndex);
    }
    std::sort(bindings.begin(), bindings.end());
    for (std::size_t first = 0; first < bindings.size();) {
        std::size_t end = first + 1;
        while (end < bindings.size() && bindings[end] == bindings[first]) {
            ++end;
        }
        const std::uint32_t objectIndex =
            static_cast<std::uint32_t>(bindings[first] >> kObjectBindingShift);
        RouteEvidence& route = output[objectIndex];
        route.hasScenario = true;
        route.ambiguous = route.ambiguous || end - first != 1;
        first = end;
    }
    return true;
}

} // namespace sunrise::client::content::activity::sdk_generation::policy_inventory::internal
