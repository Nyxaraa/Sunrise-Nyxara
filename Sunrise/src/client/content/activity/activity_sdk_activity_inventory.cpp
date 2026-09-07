#include "activity_sdk_activity_inventory.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

#include "../../../middleware/content/packages/tables/scenario_reader.h"

namespace sunrise::client::content::activity::sdk_generation::activity_inventory {
namespace {

namespace named_tags = middleware::content::packages::named_tags;
namespace reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;

/** Named scenario rows carry this package-only suffix. */
constexpr std::string_view kScenarioClientSuffix = ":scenario_client";
/** Zero and the all-ones Tiger sentinel never name a package edge. */
constexpr std::uint32_t kAbsentTag = 0xFFFFFFFFU;

/** One effective class-scan identity. */
struct LiveRoot final {
    std::uint32_t tag{};
    std::uint32_t classId{};
    std::uint32_t patchIndex{};
};

/** One copied physical named row retained only until name reconciliation finishes. */
struct NamedCandidate final {
    std::array<char, named_tags::kNameCapacity> name{};
    std::uint16_t nameLength{};
    std::uint32_t tag{};
    std::uint32_t classId{};
    std::uint32_t patchIndex{};
};

/** Heap-owned package reader storage is always closed before release. */
class ScratchOwner final {
public:
    ScratchOwner() noexcept : value_(new(std::nothrow) reader::Scratch()) {}

    ~ScratchOwner() noexcept {
        if (value_ != nullptr) {
            reader::close_files(*value_);
        }
    }

    ScratchOwner(const ScratchOwner&) = delete;
    ScratchOwner& operator=(const ScratchOwner&) = delete;

    [[nodiscard]] reader::Scratch* get() const noexcept {
        return value_.get();
    }

private:
    std::unique_ptr<reader::Scratch> value_{};
};

[[nodiscard]] bool is_cancelled(CancelProbe probe, void* context) noexcept {
    return probe != nullptr && probe(context);
}

/** Copies one effective class row out of the non-owning scan callback. */
[[nodiscard]] bool collect_live_root(void* opaque, const reader::ClassEntry& entry) noexcept {
    if (opaque == nullptr || entry.tag == 0) {
        return false;
    }
    auto& rows = *static_cast<std::vector<LiveRoot>*>(opaque);
    try {
        rows.push_back({entry.tag, 0, entry.patchIndex});
        return true;
    } catch (...) {
        return false;
    }
}

/** Copies only activity-root and scenario-root named rows. */
[[nodiscard]] bool collect_named_root(void* opaque, const named_tags::Entry& entry) noexcept {
    if (opaque == nullptr
        || (entry.classId != kActivityRootClass && entry.classId != tables::kScenarioClass)
        || entry.tag == 0 || entry.nameLength == 0 || entry.nameLength >= entry.name.size()) {
        return opaque != nullptr;
    }
    NamedCandidate row{};
    row.tag = entry.tag;
    row.classId = entry.classId;
    row.patchIndex = entry.patchIndex;
    std::string_view name(entry.name.data(), entry.nameLength);
    if (entry.classId == tables::kScenarioClass && name.ends_with(kScenarioClientSuffix)) {
        name.remove_suffix(kScenarioClientSuffix.size());
    }
    if (name.empty() || name.size() >= row.name.size()) {
        return false;
    }
    row.nameLength = static_cast<std::uint16_t>(name.size());
    std::copy(name.begin(), name.end(), row.name.begin());
    try {
        static_cast<std::vector<NamedCandidate>*>(opaque)->push_back(row);
        return true;
    } catch (...) {
        return false;
    }
}

/** Reads one little-endian u32 without assuming package-buffer alignment. */
[[nodiscard]] bool
read_u32(std::span<const std::byte> bytes, std::size_t offset, std::uint32_t& output) noexcept {
    output = 0;
    if (offset > bytes.size() || sizeof output > bytes.size() - offset) {
        return false;
    }
    std::memcpy(&output, bytes.data() + offset, sizeof output);
    return true;
}

[[nodiscard]] bool same_name(const ActivityRoot& root, std::string_view name) noexcept {
    return root.nameStatus == NameStatus::exact && root.nameLength == name.size()
           && std::equal(root.name.begin(), root.name.begin() + root.nameLength, name.begin());
}

[[nodiscard]] bool same_name(const ScenarioRoot& root, std::string_view name) noexcept {
    return root.nameStatus == NameStatus::exact && root.nameLength == name.size()
           && std::equal(root.name.begin(), root.name.begin() + root.nameLength, name.begin());
}

/** Selects a name only when its tag, class, and effective patch agree. */
template <typename Root>
[[nodiscard]] bool select_name(const std::vector<NamedCandidate>& names,
                               std::uint32_t tag,
                               std::uint32_t classId,
                               std::uint32_t effectivePatch,
                               Root& output) noexcept {
    const NamedCandidate* selected = nullptr;
    bool hasStaleAlias = false;
    for (const NamedCandidate& candidate : names) {
        if (candidate.tag != tag || candidate.classId != classId) {
            continue;
        }
        if (candidate.patchIndex != effectivePatch) {
            hasStaleAlias = true;
            continue;
        }
        if (selected == nullptr) {
            selected = &candidate;
            continue;
        }
        const std::string_view left(selected->name.data(), selected->nameLength);
        const std::string_view right(candidate.name.data(), candidate.nameLength);
        if (left != right) {
            output.nameStatus = NameStatus::ambiguous;
            return true;
        }
    }
    if (selected == nullptr) {
        output.nameStatus = hasStaleAlias ? NameStatus::staleAliasesOnly : NameStatus::unnamed;
        return true;
    }
    output.name = selected->name;
    output.nameLength = selected->nameLength;
    output.nameStatus = NameStatus::exact;
    return true;
}

[[nodiscard]] bool has_live_tag(std::span<const LiveRoot> rows, std::uint32_t tag) noexcept {
    const auto found = std::lower_bound(
        rows.begin(), rows.end(), tag, [](const LiveRoot& row, std::uint32_t value) {
            return row.tag < value;
        });
    return found != rows.end() && found->tag == tag;
}

/** Keeps allocation failures inside the package-reader callback boundary. */
[[nodiscard]] bool collect_definition(void* opaque,
                                      const tables::ActivityDefinition& definition) noexcept {
    if (opaque == nullptr) {
        return false;
    }
    try {
        static_cast<std::vector<tables::ActivityDefinition>*>(opaque)->push_back(definition);
        return true;
    } catch (...) {
        return false;
    }
}

/** Scans one exact class and stamps the class omitted by the visitor shape. */
[[nodiscard]] bool scan_live(std::wstring_view directory,
                             std::uint32_t classId,
                             std::vector<LiveRoot>& rows) noexcept {
    const std::size_t first = rows.size();
    reader::ScanResult result{};
    if (!reader::scan_class_entries(directory, classId, &collect_live_root, &rows, result)) {
        return false;
    }
    for (std::size_t index = first; index < rows.size(); ++index) {
        rows[index].classId = classId;
    }
    return result.matches == rows.size() - first;
}

/** Sorts live roots by tag and refuses zero or duplicate tags. */
[[nodiscard]] bool unique_sorted_live(std::vector<LiveRoot>& rows) noexcept {
    std::sort(rows.begin(), rows.end(), [](const LiveRoot& first, const LiveRoot& second) {
        return first.tag < second.tag;
    });
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (rows[index].tag == 0 || (index != 0 && rows[index - 1U].tag == rows[index].tag)) {
            return false;
        }
    }
    return true;
}

/** Sorts and deduplicates one tag evidence set. */
void canonicalize(std::vector<std::uint32_t>& rows) {
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
}

/** Sorts and deduplicates package locators by their serialized identity. */
void canonicalize(std::vector<PackageLocator>& rows) {
    std::sort(
        rows.begin(), rows.end(), [](const PackageLocator& first, const PackageLocator& second) {
            return first.tag < second.tag
                   || (first.tag == second.tag && first.offset < second.offset);
        });
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
}

/** Builds one join result and its complete binding evidence from source rows. */
[[nodiscard]] bool classify(const tables::ActivityDefinition& definition,
                            std::span<const ActivityRoot> roots,
                            std::span<const ScenarioRoot> scenarios,
                            ActivityVariant& output) noexcept {
    output = {};
    if (definition.internalNameLength >= definition.internalName.size()
        || definition.internalName[definition.internalNameLength] != '\0'
        || definition.hasMatchmakingConfig
               != (definition.matchmakingConfigTag
                   != tables::kActivityDefinitionNoMatchmakingConfig)) {
        return false;
    }
    try {
        output.definition = definition;
        const std::string_view name(definition.internalName.data(), definition.internalNameLength);
        ActivityBindingEvidence& evidence = output.bindingEvidence;
        evidence.hasInternalName = !name.empty();
        evidence.hasMatchmakingConfig = definition.hasMatchmakingConfig;
        evidence.matchmakingConfigTag = definition.matchmakingConfigTag;
        evidence.locators = {
            {tables::kActivityDefinitionTableTag, definition.recordOffset},
            {tables::kActivityDefinitionTableTag, definition.matchmakingPointerOffset},
            {tables::kActivityDefinitionTableTag, definition.internalNamePointerOffset},
        };
        for (const ActivityRoot& root : roots) {
            if (same_name(root, name)) {
                evidence.activityRootCandidateTags.push_back(root.tag);
            }
        }
        for (const ScenarioRoot& scenario : scenarios) {
            if (same_name(scenario, name)) {
                evidence.scenarioNameCandidateTags.push_back(scenario.tag);
            }
        }
        canonicalize(evidence.activityRootCandidateTags);
        canonicalize(evidence.scenarioNameCandidateTags);

        if (name.empty()) {
            output.joinStatus = JoinStatus::sourceNameMissing;
        } else if (evidence.activityRootCandidateTags.empty()) {
            output.joinStatus = JoinStatus::liveNameMissing;
        } else if (evidence.activityRootCandidateTags.size() == 1) {
            output.joinStatus = JoinStatus::exact;
            output.activityRootTag = evidence.activityRootCandidateTags.front();
            const auto selected =
                std::find_if(roots.begin(), roots.end(), [&output](const ActivityRoot& root) {
                    return root.tag == output.activityRootTag;
                });
            if (selected == roots.end()) {
                return false;
            }
            output.scenarioTag = selected->scenarioTag;
            evidence.locators.push_back({output.activityRootTag, kActivityRootScenarioOffset});
            evidence.locators.push_back({output.activityRootTag, kActivityRootTransitionOffset});
        } else {
            output.joinStatus = JoinStatus::liveNameAmbiguous;
        }

        evidence.evidenceRootTags = evidence.activityRootCandidateTags;
        evidence.evidenceRootTags.insert(evidence.evidenceRootTags.end(),
                                         evidence.scenarioNameCandidateTags.begin(),
                                         evidence.scenarioNameCandidateTags.end());
        if (output.activityRootTag != 0) {
            evidence.evidenceRootTags.push_back(output.activityRootTag);
        }
        if (output.scenarioTag != 0) {
            evidence.evidenceRootTags.push_back(output.scenarioTag);
        }
        canonicalize(evidence.evidenceRootTags);
        canonicalize(evidence.locators);

        switch (output.joinStatus) {
        case JoinStatus::exact:
            output.bindingDisposition = BindingDisposition::fixedScenario;
            output.bindingReason = BindingReason::exactActivityRootScenarioEdge;
            output.bindingEvidenceBasis =
                BindingEvidenceBasis::effectiveActivityRootNamePlusPayloadScenarioEdge;
            output.runnableStatus = RunnableStatus::fixedScenarioBound;
            output.fullSdkAcceptable = true;
            break;
        case JoinStatus::liveNameMissing:
            output.bindingDisposition = BindingDisposition::namedDefinitionUnavailable;
            output.bindingReason = BindingReason::installedRouteAbsent;
            output.bindingEvidenceBasis = BindingEvidenceBasis::effectiveActivityRootNameCensus;
            output.runnableStatus = RunnableStatus::unavailableInInstalledEstate;
            output.fullSdkAcceptable = true;
            break;
        case JoinStatus::sourceNameMissing:
            output.bindingDisposition = BindingDisposition::noDirectFixedActivityName;
            output.bindingReason = BindingReason::noDirectFixedActivityName;
            output.bindingEvidenceBasis = BindingEvidenceBasis::activityRecordInternalNameEmpty;
            output.runnableStatus = RunnableStatus::fixedScenarioNotApplicable;
            output.fullSdkAcceptable = true;
            break;
        case JoinStatus::liveNameAmbiguous:
            output.bindingDisposition = BindingDisposition::unresolvedRunnable;
            output.bindingReason = BindingReason::activityRootNameAmbiguous;
            output.bindingEvidenceBasis = BindingEvidenceBasis::effectiveActivityRootNameCensus;
            output.runnableStatus = RunnableStatus::unresolved;
            break;
        }
        return true;
    } catch (...) {
        output = {};
        return false;
    }
}

/** Folds one variant's join status into the run's diagnostics and completeness counters. */
void measure(const ActivityVariant& row,
             Diagnostics& diagnostics,
             BindingCompleteness& completeness) noexcept {
    ++completeness.total;
    switch (row.joinStatus) {
    case JoinStatus::exact:
        ++diagnostics.exact;
        break;
    case JoinStatus::liveNameMissing:
        ++diagnostics.liveNameMissing;
        break;
    case JoinStatus::sourceNameMissing:
        ++diagnostics.sourceNameMissing;
        break;
    case JoinStatus::liveNameAmbiguous:
        ++diagnostics.liveNameAmbiguous;
        break;
    }
    switch (row.bindingDisposition) {
    case BindingDisposition::fixedScenario:
        ++completeness.fixedScenario;
        break;
    case BindingDisposition::namedDefinitionUnavailable:
        ++completeness.namedDefinitionUnavailable;
        break;
    case BindingDisposition::noDirectFixedActivityName:
        ++completeness.noDirectFixedActivityName;
        break;
    case BindingDisposition::unresolvedRunnable:
        ++completeness.unresolvedRunnable;
        break;
    }
    completeness.status = completeness.unresolvedRunnable == 0
                              ? BindingCompletenessStatus::ready
                              : BindingCompletenessStatus::blockedUnresolvedRunnable;
}

/** @return True when both variants agree on every joined binding field. */
[[nodiscard]] bool same_binding(const ActivityVariant& left,
                                const ActivityVariant& right) noexcept {
    return left.activityRootTag == right.activityRootTag && left.scenarioTag == right.scenarioTag
           && left.joinStatus == right.joinStatus
           && left.bindingDisposition == right.bindingDisposition
           && left.bindingReason == right.bindingReason
           && left.bindingEvidenceBasis == right.bindingEvidenceBasis
           && left.runnableStatus == right.runnableStatus
           && left.fullSdkAcceptable == right.fullSdkAcceptable
           && left.bindingEvidence == right.bindingEvidence;
}

} // namespace

/** Joins already-validated activity definitions to exact selected activity-root names. */
bool join(std::span<const tables::ActivityDefinition> definitions,
          std::span<const ActivityRoot> roots,
          std::span<const ScenarioRoot> scenarios,
          std::vector<ActivityVariant>& activities,
          Diagnostics& diagnostics,
          BindingCompleteness& bindingCompleteness) noexcept {
    activities.clear();
    diagnostics = {};
    bindingCompleteness = {};
    try {
        activities.reserve(definitions.size());
        for (std::size_t index = 0; index < definitions.size(); ++index) {
            const tables::ActivityDefinition& definition = definitions[index];
            ActivityVariant row{};
            if (definition.activityIndex != index || !classify(definition, roots, scenarios, row)) {
                return false;
            }
            measure(row, diagnostics, bindingCompleteness);
            activities.push_back(std::move(row));
        }
        return true;
    } catch (...) {
        activities.clear();
        diagnostics = {};
        bindingCompleteness = {};
        return false;
    }
}

/** Validates ordering, edge closure, and recomputed join/completeness totals. */
bool validate(const Snapshot& snapshot) noexcept {
    for (std::size_t index = 0; index < snapshot.scenarios.size(); ++index) {
        const ScenarioRoot& row = snapshot.scenarios[index];
        if (row.tag == 0 || row.nameStatus != NameStatus::exact || row.nameLength == 0
            || row.nameLength >= row.name.size() || row.name[row.nameLength] != '\0'
            || (index != 0 && snapshot.scenarios[index - 1U].tag >= row.tag)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < snapshot.activityRoots.size(); ++index) {
        const ActivityRoot& row = snapshot.activityRoots[index];
        const auto scenario =
            std::lower_bound(snapshot.scenarios.begin(),
                             snapshot.scenarios.end(),
                             row.scenarioTag,
                             [](const ScenarioRoot& candidate, std::uint32_t value) {
                                 return candidate.tag < value;
                             });
        if (row.tag == 0 || row.scenarioTag == 0 || row.transitionDescriptorTag == 0
            || row.nameLength >= row.name.size() || row.name[row.nameLength] != '\0'
            || (index != 0 && snapshot.activityRoots[index - 1U].tag >= row.tag)
            || scenario == snapshot.scenarios.end() || scenario->tag != row.scenarioTag) {
            return false;
        }
    }
    Diagnostics measured{};
    BindingCompleteness measuredBindings{};
    for (std::size_t index = 0; index < snapshot.activities.size(); ++index) {
        const ActivityVariant& row = snapshot.activities[index];
        ActivityVariant expected{};
        if (row.definition.activityIndex != index
            || !classify(row.definition, snapshot.activityRoots, snapshot.scenarios, expected)
            || !same_binding(row, expected)) {
            return false;
        }
        measure(row, measured, measuredBindings);
    }
    if (measured.exact != snapshot.diagnostics.exact
        || measured.liveNameMissing != snapshot.diagnostics.liveNameMissing
        || measured.sourceNameMissing != snapshot.diagnostics.sourceNameMissing
        || measured.liveNameAmbiguous != snapshot.diagnostics.liveNameAmbiguous
        || measuredBindings != snapshot.bindingCompleteness) {
        return false;
    }
    return true;
}

/** Builds the complete native activity/root/scenario inventory from installed package data. */
bool build(const reader::Source& source,
           CancelProbe cancel,
           void* cancelContext,
           Snapshot& output) noexcept {
    output = {};
    if (source.directory.empty() || source.keys == nullptr || is_cancelled(cancel, cancelContext)) {
        return false;
    }
    try {
        std::vector<LiveRoot> liveActivityRoots{};
        std::vector<LiveRoot> liveScenarios{};
        std::vector<NamedCandidate> names{};
        reader::release_caches();
        if (!scan_live(source.directory, kActivityRootClass, liveActivityRoots)
            || is_cancelled(cancel, cancelContext)
            || !scan_live(source.directory, tables::kScenarioClass, liveScenarios)
            || !unique_sorted_live(liveActivityRoots) || !unique_sorted_live(liveScenarios)
            || is_cancelled(cancel, cancelContext)) {
            return false;
        }
        named_tags::DirectoryResult namedResult{};
        if (!named_tags::extract_directory(
                source.directory, &collect_named_root, &names, namedResult)
            || is_cancelled(cancel, cancelContext)) {
            return false;
        }

        output.scenarios.reserve(liveScenarios.size());
        for (const LiveRoot& live : liveScenarios) {
            ScenarioRoot row{};
            row.tag = live.tag;
            if (!select_name(names, live.tag, tables::kScenarioClass, live.patchIndex, row)
                || row.nameStatus != NameStatus::exact) {
                return false;
            }
            output.scenarios.push_back(row);
        }

        ScratchOwner scratch{};
        if (scratch.get() == nullptr) {
            return false;
        }
        std::vector<std::byte> bytes{};
        std::uint32_t classId = 0;
        output.activityRoots.reserve(liveActivityRoots.size());
        for (const LiveRoot& live : liveActivityRoots) {
            if (is_cancelled(cancel, cancelContext)) {
                return false;
            }
            ActivityRoot row{};
            row.tag = live.tag;
            if (!select_name(names, live.tag, kActivityRootClass, live.patchIndex, row)
                || !reader::read_tag(source, *scratch.get(), live.tag, bytes, classId)
                || classId != kActivityRootClass || bytes.size() != kActivityRootSize
                || !read_u32(bytes, kActivityRootScenarioOffset, row.scenarioTag)
                || !read_u32(bytes, kActivityRootTransitionOffset, row.transitionDescriptorTag)
                || row.scenarioTag == 0 || row.scenarioTag == kAbsentTag
                || row.transitionDescriptorTag == 0 || row.transitionDescriptorTag == kAbsentTag
                || !has_live_tag(liveScenarios, row.scenarioTag)
                || !reader::read_tag(
                    source, *scratch.get(), row.transitionDescriptorTag, bytes, classId)
                || classId != kActivityTransitionDescriptorClass
                || bytes.size() != kActivityTransitionDescriptorSize) {
                return false;
            }
            output.activityRoots.push_back(row);
        }

        if (is_cancelled(cancel, cancelContext)
            || !reader::read_tag(
                source, *scratch.get(), tables::kActivityDefinitionTableTag, bytes, classId)
            || classId != tables::kActivityDefinitionTableClass) {
            return false;
        }
        std::vector<tables::ActivityDefinition> definitions{};
        definitions.reserve(tables::kActivityDefinitionCount);
        if (!tables::visit_activity_definitions(bytes, &collect_definition, &definitions)
            || definitions.size() != tables::kActivityDefinitionCount
            || !join(definitions,
                     output.activityRoots,
                     output.scenarios,
                     output.activities,
                     output.diagnostics,
                     output.bindingCompleteness)
            || !validate(output)) {
            output = {};
            return false;
        }
        return true;
    } catch (...) {
        output = {};
        return false;
    }
}

} // namespace sunrise::client::content::activity::sdk_generation::activity_inventory
