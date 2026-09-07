#include "activity_sdk_activity_enrichment_inventory.h"

#include <algorithm>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

#include "../../../middleware/content/packages/tables/activity_display_name_reader.h"
#include "../../../state/activity_sdk/format.h"
#include "../../../state/build_data/scriptables/definition.h"

namespace sunrise::client::content::activity::sdk_generation::activity_enrichment_inventory {
namespace {

namespace display = middleware::content::packages::tables::activity_display_names;
namespace format = state::activity_sdk::format;
namespace catalog = state::build_data::scriptables;
namespace reader = middleware::content::packages::reader;

struct PackageContext final {
    const reader::Source* source{};
    reader::Scratch* scratch{};
};

struct UniqueTag final {
    std::uint32_t value{};
    std::size_t count{};
};

/** Retains one class-scan match while allowing the scanner to finish its complete census. */
[[nodiscard]] bool collect_unique_tag(void* opaque, std::uint32_t tag) noexcept {
    if (opaque == nullptr || tag == 0) {
        return false;
    }
    auto& output = *static_cast<UniqueTag*>(opaque);
    output.value = tag;
    ++output.count;
    return true;
}

/** Finds exactly one installed entry of a schema class without shipping its current tag. */
[[nodiscard]] bool find_unique_tag(const reader::Source& source,
                                   std::uint32_t classId,
                                   std::uint32_t& output) noexcept {
    output = 0;
    UniqueTag match{};
    reader::ScanResult result{};
    if (source.directory.empty()
        || !reader::scan_class(source.directory, classId, &collect_unique_tag, &match, result)
        || match.count != 1 || result.matches != 1 || match.value == 0) {
        return false;
    }
    output = match.value;
    return true;
}

/** Adapts the package reader while requiring the exact class requested by the parser. */
[[nodiscard]] bool read_tag(void* opaque,
                            std::uint32_t tag,
                            std::uint32_t expectedClass,
                            std::vector<std::byte>& output) noexcept {
    output.clear();
    if (opaque == nullptr) {
        return false;
    }
    const auto& context = *static_cast<const PackageContext*>(opaque);
    std::uint32_t actualClass = 0;
    return context.source != nullptr && context.scratch != nullptr
           && reader::read_tag(*context.source, *context.scratch, tag, output, actualClass)
           && actualClass == expectedClass;
}

/** Accepts strict UTF-8 without embedded NUL bytes. */
[[nodiscard]] bool valid_utf8(std::string_view value) noexcept {
    std::size_t index = 0;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        std::size_t count = 0;
        std::uint32_t codepoint = 0;
        std::uint32_t minimum = 0;
        if (first == 0) {
            return false;
        }
        if (first <= 0x7FU) {
            count = 1;
            codepoint = first;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            count = 2;
            codepoint = first & 0x1FU;
            minimum = 0x80U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            count = 3;
            codepoint = first & 0x0FU;
            minimum = 0x800U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            count = 4;
            codepoint = first & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (count > value.size() - index) {
            return false;
        }
        for (std::size_t offset = 1; offset < count; ++offset) {
            const auto next = static_cast<unsigned char>(value[index + offset]);
            if ((next & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3FU);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFU
            || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return false;
        }
        index += count;
    }
    return true;
}

[[nodiscard]] bool valid_text(const topology_inventory::Text& text) noexcept {
    return text.length < text.value.size() && text.value[text.length] == '\0'
           && valid_utf8(std::string_view(text.value.data(), text.length));
}

/** Copies one exact package name into SDK-owned text storage. */
[[nodiscard]] bool copy_text(const display::Name& source,
                             topology_inventory::Text& output) noexcept {
    output = {};
    if (source.length >= source.value.size() || source.length >= output.value.size()) {
        return false;
    }
    std::copy_n(source.value.begin(), source.length, output.value.begin());
    output.length = source.length;
    return valid_text(output);
}

/** Checks that a package definition agrees with the enrichment identity fields. */
[[nodiscard]] bool
same_definition(const middleware::content::packages::tables::ActivityDefinition& actual,
                const Row& expected) noexcept {
    return actual.activityIndex == expected.activityIndex
           && actual.definitionHash == expected.definitionHash
           && actual.recordLength == expected.recordLength
           && actual.requiredLevel == expected.requiredLevel
           && actual.requiredPower == expected.requiredPower
           && actual.requiredLevel2 == expected.requiredLevel2
           && actual.requiredPower2 == expected.requiredPower2
           && actual.typeIndex == expected.typeIndex;
}

} // namespace

/** Validates identity order, UTF-8, and explicit exact-versus-authored-empty accounting. */
bool validate(const Snapshot& snapshot) noexcept {
    if (snapshot.rows.empty() || snapshot.rows.size() >= format::kAbsentIndex
        || static_cast<std::size_t>(snapshot.resolvedNameCount) + snapshot.authoredEmptyNameCount
               != snapshot.rows.size()) {
        return false;
    }
    std::uint32_t resolved = 0;
    std::uint32_t authoredEmpty = 0;
    for (std::size_t index = 0; index < snapshot.rows.size(); ++index) {
        const Row& row = snapshot.rows[index];
        if (row.activityIndex != index || row.definitionHash == 0 || row.recordLength == 0
            || !valid_text(row.displayName)
            || row.authoredEmptyName != (row.displayName.length == 0)) {
            return false;
        }
        resolved += !row.authoredEmptyName;
        authoredEmpty += row.authoredEmptyName;
    }
    return resolved == snapshot.resolvedNameCount
           && authoredEmpty == snapshot.authoredEmptyNameCount;
}

/** Builds the all-activity enrichment from installed localized-string packages. */
bool build(const reader::Source& source,
           reader::Scratch& scratch,
           const activity_inventory::Snapshot& activities,
           Snapshot& output) noexcept {
    output = {};
    if (activities.activities.empty() || activities.activities.size() >= format::kAbsentIndex) {
        return false;
    }
    try {
        std::vector<std::uint32_t> hashes{};
        hashes.reserve(activities.activities.size());
        for (std::size_t index = 0; index < activities.activities.size(); ++index) {
            const auto& definition = activities.activities[index].definition;
            if (definition.activityIndex != index || definition.definitionHash == 0) {
                return false;
            }
            hashes.push_back(definition.definitionHash);
        }

        PackageContext context{&source, &scratch};
        std::uint32_t activityTableTag = 0;
        std::uint32_t stringBankIndexTag = 0;
        if (!find_unique_tag(source, display::kActivityClientTableClass, activityTableTag)
            || !find_unique_tag(source, display::kStringBankIndexClass, stringBankIndexTag)) {
            return false;
        }
        display::Snapshot names{};
        if (!display::build(
                {&context, &read_tag, activityTableTag, stringBankIndexTag}, hashes, names)
            || names.names.size() != activities.activities.size()) {
            return false;
        }

        Snapshot pending{};
        pending.resolvedNameCount = names.resolvedCount;
        pending.authoredEmptyNameCount = names.authoredEmptyCount;
        pending.rows.reserve(activities.activities.size());
        for (std::size_t index = 0; index < activities.activities.size(); ++index) {
            const auto& definition = activities.activities[index].definition;
            const display::Name& name = names.names[index];
            Row row{};
            row.activityIndex = definition.activityIndex;
            row.definitionHash = definition.definitionHash;
            if (definition.recordLength > (std::numeric_limits<std::uint32_t>::max)()) {
                return false;
            }
            row.recordLength = static_cast<std::uint32_t>(definition.recordLength);
            row.requiredLevel = definition.requiredLevel;
            row.requiredPower = definition.requiredPower;
            row.requiredLevel2 = definition.requiredLevel2;
            row.requiredPower2 = definition.requiredPower2;
            row.typeIndex = definition.typeIndex;
            row.authoredEmptyName = name.authoredEmpty;
            if (!copy_text(name, row.displayName)) {
                return false;
            }
            pending.rows.push_back(row);
        }
        if (!validate(pending) || !validate_source(activities, pending)) {
            return false;
        }
        output = std::move(pending);
        return true;
    } catch (...) {
        output = {};
        return false;
    }
}

/** Requires every native activity record to reproduce the enrichment identity fields. */
bool validate_source(const activity_inventory::Snapshot& source,
                     const Snapshot& enrichment) noexcept {
    if (!validate(enrichment) || source.activities.size() != enrichment.rows.size()) {
        return false;
    }
    for (std::size_t index = 0; index < source.activities.size(); ++index) {
        if (!same_definition(source.activities[index].definition, enrichment.rows[index])) {
            return false;
        }
    }
    return true;
}

/** Adds display names and the exact content-join flag to an otherwise closed native topology. */
bool apply(const Snapshot& enrichment, topology_inventory::Snapshot& topology) noexcept {
    // The exact-join bits a topology row must already carry before names are added.
    constexpr std::uint32_t beforeContent = format::kActivityRootExact
                                            | format::kActivityScenarioExact
                                            | format::kActivityExtractionPresent;
    if (!validate(enrichment) || !topology.ready
        || topology.activities.size() != enrichment.rows.size()) {
        return false;
    }
    for (std::size_t index = 0; index < topology.activities.size(); ++index) {
        const topology_inventory::Activity& activity = topology.activities[index];
        const Row& row = enrichment.rows[index];
        const bool joined = activity.scenarioIndex != catalog::kNoRow;
        if (activity.activityIndex != row.activityIndex
            || activity.definitionHash != row.definitionHash
            || (joined && activity.exactFlags != beforeContent)
            || (!joined && activity.exactFlags != 0)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < topology.activities.size(); ++index) {
        topology_inventory::Activity& activity = topology.activities[index];
        activity.displayName = enrichment.rows[index].displayName;
        if (activity.scenarioIndex != catalog::kNoRow) {
            activity.exactFlags |= format::kActivityContentExact;
        }
    }
    return true;
}

} // namespace sunrise::client::content::activity::sdk_generation::activity_enrichment_inventory
