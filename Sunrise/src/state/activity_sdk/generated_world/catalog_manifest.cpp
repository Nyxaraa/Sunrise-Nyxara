#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "catalog_manifest_internal.h"

namespace sunrise::state::activity_sdk::generated_world::manifest::internal {

/** Returns true for one present package tag rather than either absence representation. */
[[nodiscard]] bool valid_tag(std::uint32_t tag) noexcept {
    return tag != 0 && tag != kAbsentTag;
}

/** A generation pin may never use the all-zero failure value. */
[[nodiscard]] bool valid_digest(const Digest& digest) noexcept {
    return digest != Digest{};
}

/** Both runtime SDK digests are mandatory in every current catalog. */
[[nodiscard]] bool valid_sdk(const SdkIdentity& sdk) noexcept {
    return valid_digest(sdk.buildSha256) && valid_digest(sdk.payloadSha256);
}

/** Checks one public bounded string without requiring zero-filled trailing storage. */
template <std::size_t Capacity>
[[nodiscard]] bool
valid_text(const std::array<char, Capacity>& text, std::uint8_t length, bool required) noexcept {
    const std::size_t size = length;
    if ((required && size == 0) || size >= text.size() || text[size] != '\0') {
        return false;
    }
    return std::find(text.begin(), text.begin() + size, '\0') == text.begin() + size;
}

/** Checks one disk string including its required zero-filled trailing storage. */
template <std::size_t Capacity>
[[nodiscard]] bool valid_disk_text(const std::array<char, Capacity>& text,
                                   std::uint8_t length) noexcept {
    const std::size_t size = length;
    return size < text.size() && text[size] == '\0'
           && std::find(text.begin(), text.begin() + size, '\0') == text.begin() + size
           && std::all_of(
               text.begin() + size, text.end(), [](char value) { return value == '\0'; });
}

/** Checks one root selection enumerator before it is trusted. */
[[nodiscard]] bool valid_selection_status(ActivityRootSelectionStatus status) noexcept {
    switch (status) {
    case ActivityRootSelectionStatus::exact:
    case ActivityRootSelectionStatus::ambiguous:
    case ActivityRootSelectionStatus::staleAliasesOnly:
    case ActivityRootSelectionStatus::unnamed:
        return true;
    }
    return false;
}

/** Checks one activity join enumerator before it is trusted. */
[[nodiscard]] bool valid_join_status(ActivityJoinStatus status) noexcept {
    switch (status) {
    case ActivityJoinStatus::exact:
    case ActivityJoinStatus::liveNameMissing:
    case ActivityJoinStatus::sourceNameMissing:
    case ActivityJoinStatus::ambiguous:
        return true;
    }
    return false;
}

/** Checks one binding disposition before it is trusted. */
[[nodiscard]] bool valid_binding_disposition(BindingDisposition disposition) noexcept {
    switch (disposition) {
    case BindingDisposition::fixedScenario:
    case BindingDisposition::namedDefinitionUnavailable:
    case BindingDisposition::noDirectFixedActivityName:
    case BindingDisposition::unresolvedRunnable:
        return true;
    }
    return false;
}

/** Checks one binding reason before it is trusted. */
[[nodiscard]] bool valid_binding_reason(BindingReason reason) noexcept {
    switch (reason) {
    case BindingReason::exactActivityRootScenarioEdge:
    case BindingReason::installedRouteAbsent:
    case BindingReason::noDirectFixedActivityName:
    case BindingReason::activityRootNameAmbiguous:
    case BindingReason::activityRootEdgeMissing:
        return true;
    }
    return false;
}

/** Checks one evidence basis before it is trusted. */
[[nodiscard]] bool valid_evidence_basis(BindingEvidenceBasis basis) noexcept {
    switch (basis) {
    case BindingEvidenceBasis::effectiveActivityRootNamePlusPayloadScenarioEdge:
    case BindingEvidenceBasis::effectiveActivityAndScenarioRootNameCensus:
    case BindingEvidenceBasis::activityRecordInternalNameEmpty:
    case BindingEvidenceBasis::effectiveActivityRootNameCensus:
        return true;
    }
    return false;
}

/** Checks one runnable-state interpretation before it is trusted. */
[[nodiscard]] bool valid_runnable_status(RunnableStatus status) noexcept {
    switch (status) {
    case RunnableStatus::fixedScenarioBound:
    case RunnableStatus::unavailableInInstalledEstate:
    case RunnableStatus::fixedScenarioNotApplicable:
    case RunnableStatus::unresolved:
        return true;
    }
    return false;
}

/** Checks one whole-estate status before it is trusted. */
[[nodiscard]] bool valid_completeness_status(BindingCompletenessStatus status) noexcept {
    switch (status) {
    case BindingCompletenessStatus::ready:
    case BindingCompletenessStatus::blockedUnresolvedRunnable:
        return true;
    }
    return false;
}

/** Checks one tag set's stable ascending unique representation. */
[[nodiscard]] bool valid_tag_set(std::span<const std::uint32_t> tags) noexcept {
    for (std::size_t index = 0; index < tags.size(); ++index) {
        if (!valid_tag(tags[index]) || (index != 0 && tags[index - 1U] >= tags[index])) {
            return false;
        }
    }
    return true;
}

/** Checks one locator set's stable ascending unique representation. */
[[nodiscard]] bool valid_locators(std::span<const PackageLocator> locators) noexcept {
    if (locators.empty() || locators.size() > kMaximumPackageLocatorsPerVariant) {
        return false;
    }
    for (std::size_t index = 0; index < locators.size(); ++index) {
        const PackageLocator& locator = locators[index];
        if (!valid_tag(locator.tag)
            || (index != 0
                && !(std::pair(locators[index - 1U].tag, locators[index - 1U].offset)
                     < std::pair(locator.tag, locator.offset)))) {
            return false;
        }
    }
    return true;
}

/** Checks one canonical public scenario row before it reaches disk. */
[[nodiscard]] bool valid_scenario(const ScenarioRecord& record) noexcept {
    return valid_tag(record.scenarioTag)
           && valid_text(record.scenarioName, record.scenarioNameLength, true);
}

/** Checks one canonical public activity-root row before it reaches disk. */
[[nodiscard]] bool valid_activity_root(const ActivityRootRecord& record) noexcept {
    if (!valid_tag(record.activityRootTag) || !valid_tag(record.scenarioTag)
        || !valid_tag(record.transitionDescriptorTag)
        || !valid_selection_status(record.selectionStatus)) {
        return false;
    }
    const bool exact = record.selectionStatus == ActivityRootSelectionStatus::exact;
    return valid_text(record.preferredName, record.preferredNameLength, exact)
           && (exact || record.preferredNameLength == 0);
}

/** Checks one canonical public activity-variant row before it reaches disk. */
[[nodiscard]] bool valid_activity_variant(const ActivityVariantRecord& record) noexcept {
    if (record.activityIndex == kAbsentTag || !valid_join_status(record.joinStatus)
        || !valid_binding_disposition(record.bindingDisposition)
        || !valid_binding_reason(record.bindingReason)
        || !valid_evidence_basis(record.bindingEvidenceBasis)
        || !valid_runnable_status(record.runnableStatus)
        || record.hasInternalName != (record.internalNameLength != 0)
        || record.hasMatchmakingConfig != valid_tag(record.matchmakingConfigTag)
        || (!record.hasMatchmakingConfig && record.matchmakingConfigTag != kAbsentTag)
        || record.fullSdkAcceptable
               != (record.bindingDisposition != BindingDisposition::unresolvedRunnable)
        || !valid_tag_set(record.activityRootCandidateTags)
        || !valid_tag_set(record.scenarioNameCandidateTags)
        || !valid_tag_set(record.evidenceRootTags) || !valid_locators(record.locators)) {
        return false;
    }
    const bool exact = record.joinStatus == ActivityJoinStatus::exact;
    const bool sourceNameMissing = record.joinStatus == ActivityJoinStatus::sourceNameMissing;
    if (!valid_text(record.internalName, record.internalNameLength, !sourceNameMissing)
        || (sourceNameMissing && record.internalNameLength != 0)) {
        return false;
    }
    return exact ? valid_tag(record.activityRootTag) && valid_tag(record.scenarioTag)
                 : record.activityRootTag == kAbsentTag && record.scenarioTag == kAbsentTag;
}

/** Copies one public scenario into its zero-filled disk form. */
[[nodiscard]] bool encode_scenario(const ScenarioRecord& input,
                                   DiskScenarioRecord& output) noexcept {
    if (!valid_scenario(input)) {
        return false;
    }
    output = {};
    output.scenarioTag = input.scenarioTag;
    output.scenarioNameLength = input.scenarioNameLength;
    std::memcpy(output.scenarioName.data(), input.scenarioName.data(), input.scenarioNameLength);
    output.shardPayloadSha256 = input.shardPayloadSha256;
    return true;
}

/** Decodes one scenario only when all reserved and text bytes are canonical. */
[[nodiscard]] bool decode_scenario(const DiskScenarioRecord& input,
                                   ScenarioRecord& output) noexcept {
    if (input.reserved != std::array<std::uint8_t, 3>{}
        || !valid_disk_text(input.scenarioName, input.scenarioNameLength)) {
        return false;
    }
    output = {};
    output.scenarioTag = input.scenarioTag;
    output.scenarioNameLength = input.scenarioNameLength;
    std::memcpy(output.scenarioName.data(), input.scenarioName.data(), input.scenarioNameLength);
    output.shardPayloadSha256 = input.shardPayloadSha256;
    return valid_scenario(output);
}

/** Copies one public activity root into its zero-filled disk form. */
[[nodiscard]] bool encode_activity_root(const ActivityRootRecord& input,
                                        DiskActivityRootRecord& output) noexcept {
    if (!valid_activity_root(input)) {
        return false;
    }
    output = {};
    output.activityRootTag = input.activityRootTag;
    output.scenarioTag = input.scenarioTag;
    output.transitionDescriptorTag = input.transitionDescriptorTag;
    output.preferredNameLength = input.preferredNameLength;
    output.selectionStatus = static_cast<std::uint8_t>(input.selectionStatus);
    std::memcpy(output.preferredName.data(), input.preferredName.data(), input.preferredNameLength);
    return true;
}

/** Decodes one activity root only when its disk representation is canonical. */
[[nodiscard]] bool decode_activity_root(const DiskActivityRootRecord& input,
                                        ActivityRootRecord& output) noexcept {
    if (input.reserved != std::array<std::uint8_t, 2>{}
        || !valid_disk_text(input.preferredName, input.preferredNameLength)) {
        return false;
    }
    output = {};
    output.activityRootTag = input.activityRootTag;
    output.scenarioTag = input.scenarioTag;
    output.transitionDescriptorTag = input.transitionDescriptorTag;
    output.preferredNameLength = input.preferredNameLength;
    output.selectionStatus = static_cast<ActivityRootSelectionStatus>(input.selectionStatus);
    std::memcpy(output.preferredName.data(), input.preferredName.data(), input.preferredNameLength);
    return valid_activity_root(output);
}

/** Copies one public activity variant and appends its canonical evidence sections. */
[[nodiscard]] bool encode_activity_variant(const ActivityVariantRecord& input,
                                           std::vector<std::uint32_t>& evidenceTags,
                                           std::vector<DiskPackageLocator>& packageLocators,
                                           DiskActivityVariantRecord& output) noexcept {
    if (!valid_activity_variant(input)) {
        return false;
    }
    const auto append_tags = [&evidenceTags](std::span<const std::uint32_t> values,
                                             std::uint32_t& first,
                                             std::uint32_t& count) {
        if (evidenceTags.size() > (std::numeric_limits<std::uint32_t>::max)()
            || values.size() > (std::numeric_limits<std::uint32_t>::max)()
            || evidenceTags.size() > kMaximumEvidenceTags
            || values.size() > kMaximumEvidenceTags - evidenceTags.size()) {
            return false;
        }
        first = static_cast<std::uint32_t>(evidenceTags.size());
        count = static_cast<std::uint32_t>(values.size());
        evidenceTags.insert(evidenceTags.end(), values.begin(), values.end());
        return true;
    };
    output = {};
    output.activityIndex = input.activityIndex;
    output.definitionHash = input.definitionHash;
    output.activityRootTag = input.activityRootTag;
    output.scenarioTag = input.scenarioTag;
    output.matchmakingConfigTag = input.matchmakingConfigTag;
    if (!append_tags(input.activityRootCandidateTags,
                     output.firstActivityRootCandidate,
                     output.activityRootCandidateCount)
        || !append_tags(input.scenarioNameCandidateTags,
                        output.firstScenarioNameCandidate,
                        output.scenarioNameCandidateCount)
        || !append_tags(
            input.evidenceRootTags, output.firstEvidenceRootTag, output.evidenceRootTagCount)
        || packageLocators.size() > (std::numeric_limits<std::uint32_t>::max)()
        || input.locators.size() > (std::numeric_limits<std::uint32_t>::max)()
        || packageLocators.size() > kMaximumPackageLocators
        || input.locators.size() > kMaximumPackageLocators - packageLocators.size()) {
        return false;
    }
    output.firstPackageLocator = static_cast<std::uint32_t>(packageLocators.size());
    output.packageLocatorCount = static_cast<std::uint32_t>(input.locators.size());
    for (const PackageLocator& locator : input.locators) {
        packageLocators.push_back({locator.tag, 0, locator.offset});
    }
    output.internalNameLength = input.internalNameLength;
    output.joinStatus = static_cast<std::uint8_t>(input.joinStatus);
    output.bindingDisposition = static_cast<std::uint8_t>(input.bindingDisposition);
    output.bindingReason = static_cast<std::uint8_t>(input.bindingReason);
    output.bindingEvidenceBasis = static_cast<std::uint8_t>(input.bindingEvidenceBasis);
    output.runnableStatus = static_cast<std::uint8_t>(input.runnableStatus);
    output.flags = static_cast<std::uint8_t>((input.fullSdkAcceptable ? 1U : 0U)
                                             | (input.hasInternalName ? 2U : 0U)
                                             | (input.hasMatchmakingConfig ? 4U : 0U));
    std::memcpy(output.internalName.data(), input.internalName.data(), input.internalNameLength);
    return true;
}

/** Decodes one activity variant only when all spans are canonical and contiguous. */
[[nodiscard]] bool decode_activity_variant(const DiskActivityVariantRecord& input,
                                           std::span<const std::uint32_t> evidenceTags,
                                           std::span<const DiskPackageLocator> packageLocators,
                                           std::size_t& nextEvidenceTag,
                                           std::size_t& nextPackageLocator,
                                           ActivityVariantRecord& output) noexcept {
    const auto consume = [&evidenceTags, &nextEvidenceTag](std::uint32_t first,
                                                           std::uint32_t count,
                                                           std::vector<std::uint32_t>& values) {
        const std::size_t begin = first;
        const std::size_t size = count;
        if (begin != nextEvidenceTag || begin > evidenceTags.size()
            || size > evidenceTags.size() - begin) {
            return false;
        }
        const auto offset = static_cast<std::ptrdiff_t>(begin);
        const auto span = static_cast<std::ptrdiff_t>(size);
        try {
            values.assign(evidenceTags.begin() + offset, evidenceTags.begin() + offset + span);
        } catch (...) {
            return false;
        }
        nextEvidenceTag += size;
        return true;
    };
    if (input.reserved != 0 || (input.flags & ~0x07U) != 0
        || !valid_disk_text(input.internalName, input.internalNameLength)) {
        return false;
    }
    output = {};
    output.activityIndex = input.activityIndex;
    output.definitionHash = input.definitionHash;
    output.activityRootTag = input.activityRootTag;
    output.scenarioTag = input.scenarioTag;
    output.matchmakingConfigTag = input.matchmakingConfigTag;
    output.internalNameLength = input.internalNameLength;
    output.joinStatus = static_cast<ActivityJoinStatus>(input.joinStatus);
    output.bindingDisposition = static_cast<BindingDisposition>(input.bindingDisposition);
    output.bindingReason = static_cast<BindingReason>(input.bindingReason);
    output.bindingEvidenceBasis = static_cast<BindingEvidenceBasis>(input.bindingEvidenceBasis);
    output.runnableStatus = static_cast<RunnableStatus>(input.runnableStatus);
    output.fullSdkAcceptable = (input.flags & 1U) != 0;
    output.hasInternalName = (input.flags & 2U) != 0;
    output.hasMatchmakingConfig = (input.flags & 4U) != 0;
    std::memcpy(output.internalName.data(), input.internalName.data(), input.internalNameLength);
    if (!consume(input.firstActivityRootCandidate,
                 input.activityRootCandidateCount,
                 output.activityRootCandidateTags)
        || !consume(input.firstScenarioNameCandidate,
                    input.scenarioNameCandidateCount,
                    output.scenarioNameCandidateTags)
        || !consume(
            input.firstEvidenceRootTag, input.evidenceRootTagCount, output.evidenceRootTags)) {
        return false;
    }
    const std::size_t locatorFirst = input.firstPackageLocator;
    const std::size_t locatorCount = input.packageLocatorCount;
    if (locatorFirst != nextPackageLocator || locatorFirst > packageLocators.size()
        || locatorCount > packageLocators.size() - locatorFirst) {
        return false;
    }
    try {
        output.locators.reserve(locatorCount);
        for (std::size_t index = locatorFirst; index < locatorFirst + locatorCount; ++index) {
            if (packageLocators[index].reserved != 0) {
                return false;
            }
            output.locators.push_back({packageLocators[index].tag, packageLocators[index].offset});
        }
    } catch (...) {
        return false;
    }
    nextPackageLocator += locatorCount;
    return valid_activity_variant(output);
}

/** Returns the exact public text represented by one bounded array and length. */
template <std::size_t Capacity>
[[nodiscard]] std::string_view text_view(const std::array<char, Capacity>& text,
                                         std::uint8_t length) noexcept {
    return {text.data(), length};
}

/** Orders activity variants by their stable source identity. */
[[nodiscard]] bool variant_less(const ActivityVariantRecord& left,
                                const ActivityVariantRecord& right) noexcept {
    return std::pair(left.activityIndex, left.definitionHash)
           < std::pair(right.activityIndex, right.definitionHash);
}

/** Looks up one sorted scenario row by package tag. */
[[nodiscard]] const ScenarioRecord* find_scenario(std::span<const ScenarioRecord> scenarios,
                                                  std::uint32_t scenarioTag) noexcept {
    const auto found = std::lower_bound(
        scenarios.begin(),
        scenarios.end(),
        scenarioTag,
        [](const ScenarioRecord& record, std::uint32_t tag) { return record.scenarioTag < tag; });
    return found != scenarios.end() && found->scenarioTag == scenarioTag ? &*found : nullptr;
}

/** Looks up one sorted activity-root row by package tag. */
[[nodiscard]] const ActivityRootRecord*
find_activity_root(std::span<const ActivityRootRecord> roots,
                   std::uint32_t activityRootTag) noexcept {
    const auto found = std::lower_bound(roots.begin(),
                                        roots.end(),
                                        activityRootTag,
                                        [](const ActivityRootRecord& record, std::uint32_t tag) {
                                            return record.activityRootTag < tag;
                                        });
    return found != roots.end() && found->activityRootTag == activityRootTag ? &*found : nullptr;
}

/** Returns whether one canonical locator set has exactly the native producer's tag shape. */
[[nodiscard]] bool valid_locator_evidence(const ActivityVariantRecord& variant) noexcept {
    std::size_t activityLocators = 0;
    bool hasScenarioEdge = false;
    bool hasTransitionEdge = false;
    for (const PackageLocator& locator : variant.locators) {
        if (locator.tag == kActivityDefinitionTableTag) {
            ++activityLocators;
        } else if (variant.joinStatus == ActivityJoinStatus::exact
                   && locator.tag == variant.activityRootTag
                   && locator.offset == kActivityRootScenarioOffset) {
            hasScenarioEdge = true;
        } else if (variant.joinStatus == ActivityJoinStatus::exact
                   && locator.tag == variant.activityRootTag
                   && locator.offset == kActivityRootTransitionOffset) {
            hasTransitionEdge = true;
        } else {
            return false;
        }
    }
    return activityLocators >= 1 && activityLocators <= 3
           && (variant.joinStatus == ActivityJoinStatus::exact
                   ? hasScenarioEdge && hasTransitionEdge
                         && variant.locators.size() == activityLocators + 2U
                   : variant.locators.size() == activityLocators);
}

/** Reconstructs every candidate set and classification from catalog roots and names. */
[[nodiscard]] bool valid_binding(std::span<const ScenarioRecord> scenarios,
                                 std::span<const ActivityRootRecord> roots,
                                 const ActivityVariantRecord& variant) noexcept {
    if (!valid_activity_variant(variant) || !valid_locator_evidence(variant)) {
        return false;
    }
    try {
        const std::string_view name = text_view(variant.internalName, variant.internalNameLength);
        std::vector<std::uint32_t> rootCandidates;
        std::vector<std::uint32_t> scenarioCandidates;
        for (const ActivityRootRecord& root : roots) {
            if (root.selectionStatus == ActivityRootSelectionStatus::exact
                && text_view(root.preferredName, root.preferredNameLength) == name) {
                rootCandidates.push_back(root.activityRootTag);
            }
        }
        for (const ScenarioRecord& scenario : scenarios) {
            if (text_view(scenario.scenarioName, scenario.scenarioNameLength) == name) {
                scenarioCandidates.push_back(scenario.scenarioTag);
            }
        }
        if (variant.activityRootCandidateTags != rootCandidates
            || variant.scenarioNameCandidateTags != scenarioCandidates) {
            return false;
        }

        ActivityJoinStatus expectedJoin = ActivityJoinStatus::sourceNameMissing;
        if (!name.empty()) {
            expectedJoin = rootCandidates.empty()
                               ? ActivityJoinStatus::liveNameMissing
                               : (rootCandidates.size() == 1 ? ActivityJoinStatus::exact
                                                             : ActivityJoinStatus::ambiguous);
        }
        if (variant.joinStatus != expectedJoin) {
            return false;
        }

        std::vector<std::uint32_t> expectedEvidence = rootCandidates;
        expectedEvidence.insert(
            expectedEvidence.end(), scenarioCandidates.begin(), scenarioCandidates.end());
        if (expectedJoin == ActivityJoinStatus::exact) {
            const ActivityRootRecord* root = find_activity_root(roots, rootCandidates.front());
            if (root == nullptr || variant.activityRootTag != root->activityRootTag
                || variant.scenarioTag != root->scenarioTag) {
                return false;
            }
            expectedEvidence.push_back(root->activityRootTag);
            expectedEvidence.push_back(root->scenarioTag);
        }
        std::sort(expectedEvidence.begin(), expectedEvidence.end());
        expectedEvidence.erase(std::unique(expectedEvidence.begin(), expectedEvidence.end()),
                               expectedEvidence.end());
        if (variant.evidenceRootTags != expectedEvidence) {
            return false;
        }

        BindingDisposition disposition = BindingDisposition::unresolvedRunnable;
        BindingReason reason = BindingReason::activityRootEdgeMissing;
        BindingEvidenceBasis basis =
            BindingEvidenceBasis::effectiveActivityAndScenarioRootNameCensus;
        RunnableStatus runnable = RunnableStatus::unresolved;
        bool acceptable = false;
        switch (expectedJoin) {
        case ActivityJoinStatus::exact:
            disposition = BindingDisposition::fixedScenario;
            reason = BindingReason::exactActivityRootScenarioEdge;
            basis = BindingEvidenceBasis::effectiveActivityRootNamePlusPayloadScenarioEdge;
            runnable = RunnableStatus::fixedScenarioBound;
            acceptable = true;
            break;
        case ActivityJoinStatus::liveNameMissing:
            disposition = BindingDisposition::namedDefinitionUnavailable;
            reason = BindingReason::installedRouteAbsent;
            basis = BindingEvidenceBasis::effectiveActivityRootNameCensus;
            runnable = RunnableStatus::unavailableInInstalledEstate;
            acceptable = true;
            break;
        case ActivityJoinStatus::sourceNameMissing:
            disposition = BindingDisposition::noDirectFixedActivityName;
            reason = BindingReason::noDirectFixedActivityName;
            basis = BindingEvidenceBasis::activityRecordInternalNameEmpty;
            runnable = RunnableStatus::fixedScenarioNotApplicable;
            acceptable = true;
            break;
        case ActivityJoinStatus::ambiguous:
            reason = BindingReason::activityRootNameAmbiguous;
            basis = BindingEvidenceBasis::effectiveActivityRootNameCensus;
            break;
        }
        return variant.bindingDisposition == disposition && variant.bindingReason == reason
               && variant.bindingEvidenceBasis == basis && variant.runnableStatus == runnable
               && variant.fullSdkAcceptable == acceptable;
    } catch (...) {
        return false;
    }
}

/** Derives the immutable whole-estate partition from canonical activity rows. */
[[nodiscard]] bool derive_binding_completeness(std::span<const ActivityVariantRecord> variants,
                                               BindingCompleteness& output) noexcept {
    output = {};
    output.total = variants.size();
    for (const ActivityVariantRecord& variant : variants) {
        switch (variant.bindingDisposition) {
        case BindingDisposition::fixedScenario:
            ++output.fixedScenario;
            break;
        case BindingDisposition::namedDefinitionUnavailable:
            ++output.namedDefinitionUnavailable;
            break;
        case BindingDisposition::noDirectFixedActivityName:
            ++output.noDirectFixedActivityName;
            break;
        case BindingDisposition::unresolvedRunnable:
            ++output.unresolvedRunnable;
            break;
        default:
            return false;
        }
    }
    output.status = output.unresolvedRunnable == 0
                        ? BindingCompletenessStatus::ready
                        : BindingCompletenessStatus::blockedUnresolvedRunnable;
    return output.fixedScenario + output.namedDefinitionUnavailable
               + output.noDirectFixedActivityName + output.unresolvedRunnable
           == output.total;
}

/** Encodes one bounded completeness summary into its canonical disk form. */
[[nodiscard]] bool encode_binding_completeness(const BindingCompleteness& input,
                                               DiskBindingCompleteness& output) noexcept {
    if (!valid_completeness_status(input.status) || input.total > kMaximumActivityVariantRecords
        || input.fixedScenario > input.total || input.namedDefinitionUnavailable > input.total
        || input.noDirectFixedActivityName > input.total || input.unresolvedRunnable > input.total
        || input.fixedScenario + input.namedDefinitionUnavailable + input.noDirectFixedActivityName
                   + input.unresolvedRunnable
               != input.total
        || input.status
               != (input.unresolvedRunnable == 0
                       ? BindingCompletenessStatus::ready
                       : BindingCompletenessStatus::blockedUnresolvedRunnable)) {
        return false;
    }
    output = {};
    output.total = static_cast<std::uint32_t>(input.total);
    output.fixedScenario = static_cast<std::uint32_t>(input.fixedScenario);
    output.namedDefinitionUnavailable =
        static_cast<std::uint32_t>(input.namedDefinitionUnavailable);
    output.noDirectFixedActivityName = static_cast<std::uint32_t>(input.noDirectFixedActivityName);
    output.unresolvedRunnable = static_cast<std::uint32_t>(input.unresolvedRunnable);
    output.status = static_cast<std::uint8_t>(input.status);
    return true;
}

/** Decodes one summary only when all enum and reserved bytes are canonical. */
[[nodiscard]] bool decode_binding_completeness(const DiskBindingCompleteness& input,
                                               BindingCompleteness& output) noexcept {
    if (input.reserved != std::array<std::uint8_t, 3>{}) {
        return false;
    }
    output = {input.total,
              input.fixedScenario,
              input.namedDefinitionUnavailable,
              input.noDirectFixedActivityName,
              input.unresolvedRunnable,
              static_cast<BindingCompletenessStatus>(input.status)};
    DiskBindingCompleteness canonical{};
    return encode_binding_completeness(output, canonical) && canonical.total == input.total
           && canonical.fixedScenario == input.fixedScenario
           && canonical.namedDefinitionUnavailable == input.namedDefinitionUnavailable
           && canonical.noDirectFixedActivityName == input.noDirectFixedActivityName
           && canonical.unresolvedRunnable == input.unresolvedRunnable
           && canonical.status == input.status;
}

/** Validates ordering, uniqueness, edges, evidence, and the reconstructed partition. */
[[nodiscard]] bool valid_catalog(std::span<const ScenarioRecord> scenarios,
                                 std::span<const ActivityRootRecord> roots,
                                 std::span<const ActivityVariantRecord> variants,
                                 BindingCompleteness* completeness) noexcept {
    if (scenarios.size() > kMaximumScenarioRecords || roots.size() > kMaximumActivityRootRecords
        || variants.size() > kMaximumActivityVariantRecords) {
        return false;
    }
    for (std::size_t index = 0; index < scenarios.size(); ++index) {
        if (!valid_scenario(scenarios[index])
            || (index != 0 && scenarios[index - 1U].scenarioTag >= scenarios[index].scenarioTag)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < roots.size(); ++index) {
        const ActivityRootRecord& root = roots[index];
        if (!valid_activity_root(root)
            || (index != 0 && roots[index - 1U].activityRootTag >= root.activityRootTag)
            || find_scenario(scenarios, root.scenarioTag) == nullptr) {
            return false;
        }
    }
    for (std::size_t index = 0; index < variants.size(); ++index) {
        const ActivityVariantRecord& variant = variants[index];
        if (!valid_binding(scenarios, roots, variant)
            || (index != 0 && !variant_less(variants[index - 1U], variant))) {
            return false;
        }
        if (variant.joinStatus != ActivityJoinStatus::exact) {
            continue;
        }
        const ActivityRootRecord* root = find_activity_root(roots, variant.activityRootTag);
        if (root == nullptr || root->scenarioTag != variant.scenarioTag
            || root->selectionStatus != ActivityRootSelectionStatus::exact
            || text_view(root->preferredName, root->preferredNameLength)
                   != text_view(variant.internalName, variant.internalNameLength)) {
            return false;
        }
    }
    BindingCompleteness measured{};
    if (!derive_binding_completeness(variants, measured)) {
        return false;
    }
    if (completeness != nullptr) {
        *completeness = measured;
    }
    return true;
}

} // namespace sunrise::state::activity_sdk::generated_world::manifest::internal
