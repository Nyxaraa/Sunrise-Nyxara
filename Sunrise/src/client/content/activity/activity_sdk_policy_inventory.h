#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../../state/activity_sdk/format.h"

namespace sunrise::client::content::activity::sdk_generation::policy_inventory {

namespace format = state::activity_sdk::format;

/** Format-safety bounds reject pathological installed inputs without replaying captured totals. */
inline constexpr std::size_t kMaximumActivityCount = 1U << 12U;
inline constexpr std::size_t kMaximumScenarioCount = 1U << 12U;
inline constexpr std::size_t kMaximumObjectCount = 1U << 20U;
inline constexpr std::size_t kMaximumOccurrenceCount = 1U << 22U;
inline constexpr std::size_t kMaximumSlotCount = 1U << 22U;
inline constexpr std::size_t kMaximumPolicyTextCount = 1U << 22U;
inline constexpr std::size_t kMaximumPolicyCapabilityCount = 1U << 22U;
inline constexpr std::size_t kMaximumPolicyGateCount = 1U << 24U;
inline constexpr std::size_t kMaximumPolicyRefusalCount = 1U << 22U;
inline constexpr std::size_t kMaximumHostSurfaceCount = 6;
inline constexpr std::size_t kMaximumTextBytes = 512;

/** One owned string-pool handle retained until final pack string linking. */
struct Text final {
    std::uint32_t stringIndex{};

    bool operator==(const Text&) const = default;
};

/** The four activity-to-scenario identity outcomes used by mission binding policy. */
enum class ActivityJoinStatus : std::uint8_t {
    exact,
    sourceNameMissing,
    liveNameMissing,
    liveNameAmbiguous,
};

/** Slot extraction distinguishes a complete empty walk from a partial walk. */
enum class ExtractionStatus : std::uint8_t {
    complete,
    completeEmpty,
    partial,
};

/** One final activity row with the names and join outcome consumed by policy. */
struct ActivityInput final {
    std::uint32_t activityIndex{};
    std::string_view id{};
    std::string_view internalName{};
    std::string_view displayName{};
    ActivityJoinStatus joinStatus{ActivityJoinStatus::liveNameMissing};
};

/** One final slot row with all schema and verification facts consumed by policy. */
struct SlotInput final {
    std::uint32_t objectIndex{format::kAbsentIndex};
    std::uint32_t slotIndex{};
    std::uint32_t slotType{};
    std::uint32_t componentClass{format::kAbsentIndex};
    std::uint32_t senseSchema{format::kAbsentIndex};
    std::uint32_t authSchema{format::kAbsentIndex};
    std::string_view id{};
    std::string_view name{};
    std::string_view senseSchemaId{};
    std::string_view authSchemaId{};
    ExtractionStatus extractionStatus{ExtractionStatus::complete};
    std::uint32_t flags{};
};

/** One extra slot-name candidate joined to a final slot row. */
struct SlotAliasInput final {
    std::uint32_t slotIndex{format::kAbsentIndex};
    std::string_view value{};
};

/** One complete occurrence binding used by the type-23 route cardinality gate. */
struct OccurrenceInput final {
    std::uint32_t scenarioIndex{format::kAbsentIndex};
    std::uint32_t objectIndex{format::kAbsentIndex};
};

/** One host API declaration whose operation selects the compiled adapter gate set. */
struct HostSurface final {
    std::string_view id{};
    std::string_view operation{};
};

/** All inputs must use final parent row order and complete name, schema, and route evidence. */
struct Inputs final {
    std::span<const ActivityInput> activities{};
    std::span<const SlotInput> slots{};
    std::span<const SlotAliasInput> slotAliases{};
    std::span<const OccurrenceInput> occurrences{};
    std::span<const HostSurface> hostSurfaces{};
    std::uint32_t scenarioCount{};
    std::uint32_t objectCount{};
};

/** One format-v12 Text row before its string offset is linked. */
struct TextRow final {
    Text value{};
    std::uint32_t kind{};
    std::uint32_t reserved{};

    bool operator==(const TextRow&) const = default;
};

/** One format-v12 Capability row before its strings are linked. */
struct Capability final {
    Text id{};
    Text operation{};
    Text valueSchemaId{};
    std::uint32_t subjectKind{};
    std::uint32_t subjectIndex{};
    std::uint32_t exposureFlags{};
    std::uint32_t candidateExposureFlags{};
    format::Range gates{};
    format::Range refusals{};

    /** @return True when all deferred fields and owned ranges match. */
    [[nodiscard]] bool operator==(const Capability& other) const noexcept {
        return id == other.id && operation == other.operation
               && valueSchemaId == other.valueSchemaId && subjectKind == other.subjectKind
               && subjectIndex == other.subjectIndex && exposureFlags == other.exposureFlags
               && candidateExposureFlags == other.candidateExposureFlags
               && gates.first == other.gates.first && gates.count == other.gates.count
               && refusals.first == other.refusals.first && refusals.count == other.refusals.count;
    }
};

/** One format-v12 Gate row before its strings are linked. */
struct Gate final {
    Text gate{};
    Text status{};
    Text reasonCode{};
    Text required{};
    Text observed{};
    Text wouldConfirm{};

    bool operator==(const Gate&) const = default;
};

/** One format-v12 Refusal row before its strings are linked. */
struct Refusal final {
    Text id{};
    Text exposure{};
    Text status{};
    format::Range reasonCodes{};
    std::uint32_t capabilityIndex{};
    std::uint32_t reserved{};

    /** @return True when all deferred fields, scalars, and the reason range match. */
    [[nodiscard]] bool operator==(const Refusal& other) const noexcept {
        return id == other.id && exposure == other.exposure && status == other.status
               && reasonCodes.first == other.reasonCodes.first
               && reasonCodes.count == other.reasonCodes.count
               && capabilityIndex == other.capabilityIndex && reserved == other.reserved;
    }
};

/** One sorted host subject and its contiguous capability range. */
struct HostSubject final {
    Text id{};
    format::Range capabilities{};

    /** @return True when the deferred id and capability range match. */
    [[nodiscard]] bool operator==(const HostSubject& other) const noexcept {
        return id == other.id && capabilities.first == other.capabilities.first
               && capabilities.count == other.capabilities.count;
    }
};

/** Canonical sections 8 through 11 and every range owned by earlier parent rows. */
struct Snapshot final {
    std::vector<std::string> strings{};
    std::vector<TextRow> texts{};
    std::vector<Capability> capabilities{};
    std::vector<Gate> gates{};
    std::vector<Refusal> refusals{};
    std::vector<format::Range> activityAliases{};
    std::vector<format::Range> activityCapabilities{};
    std::vector<format::Range> slotAliases{};
    std::vector<format::Range> slotCapabilities{};
    std::vector<HostSubject> hostSubjects{};

    /** @return The owned UTF-8 value or an empty view for an invalid handle. */
    [[nodiscard]] std::string_view value(Text text) const noexcept;
};

/** @return The five compiled host surfaces whose policy is emitted by this build. */
[[nodiscard]] std::span<const HostSurface> host_surfaces() noexcept;

/** Builds canonical policy rows transactionally from complete final parent inventories. */
[[nodiscard]] bool build(const Inputs& inputs, Snapshot& output) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::policy_inventory
