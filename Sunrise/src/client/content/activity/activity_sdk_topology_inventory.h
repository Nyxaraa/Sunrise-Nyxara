#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../../state/activity_sdk/format.h"
#include "../../../state/build_data/scriptables/definition.h"
#include "activity_sdk_activity_inventory.h"

namespace sunrise::client::content::activity::sdk_generation::topology_inventory {

/** A partial source walk leaves every format-v12 object count pending. */
inline constexpr std::uint32_t kObjectConfigCountPending = 0x01U;
inline constexpr std::uint32_t kObjectPlacedSubblockCountPending = 0x02U;
inline constexpr std::uint32_t kObjectPlacedLeafCountPending = 0x04U;
inline constexpr std::uint32_t kObjectPlacedHopCountPending = 0x08U;
inline constexpr std::uint32_t kObjectBareTargetCountPending = 0x10U;
inline constexpr std::uint32_t kObjectCountPendingMask =
    kObjectConfigCountPending | kObjectPlacedSubblockCountPending | kObjectPlacedLeafCountPending
    | kObjectPlacedHopCountPending | kObjectBareTargetCountPending;

/** Generated topology text stays owned until the final string table is linked. */
struct Text final {
    std::array<char, middleware::content::packages::tables::kActivityDefinitionInternalNameCapacity>
        value{};
    std::uint16_t length{};
};

/** One all-activity row before string offsets and policy ranges are linked. */
struct Activity final {
    std::uint32_t activityIndex{};
    std::uint32_t definitionHash{};
    std::uint32_t scenarioIndex{state::build_data::scriptables::kNoRow};
    /** Only exactness bits proved by this topology slice are set. */
    std::uint32_t exactFlags{};
    Text id{};
    Text internalName{};
    /** Reserved for later enrichment; this topology slice leaves it empty. */
    Text displayName{};
};

/** One scenario and its contiguous global topology ranges. */
struct Scenario final {
    std::uint32_t tag{};
    Text id{};
    Text name{};
    std::uint32_t firstBubble{};
    std::uint32_t bubbleCount{};
    std::uint32_t firstState{};
    std::uint32_t stateCount{};
    std::uint32_t firstOccurrence{};
    std::uint32_t occurrenceCount{};
};

/** One scenario bubble and its contiguous global state range. */
struct Bubble final {
    std::uint32_t scenarioIndex{};
    std::uint32_t bubbleOrdinal{};
    std::uint32_t nameHash{};
    Text id{};
    /** Raw candidates stay unresolved until the estate read universe is proved. */
    std::uint32_t observedNameRow{state::build_data::scriptables::kNoRow};
    std::uint32_t firstObservedAlias{};
    std::uint32_t observedAliasCount{};
    std::uint32_t firstState{};
    std::uint32_t stateCount{};
};

/** One exact state row retained before global string offsets are linked. */
struct State final {
    std::uint32_t scenarioIndex{};
    std::uint32_t bubbleIndex{};
    std::uint32_t stateOrdinal{};
    std::uint32_t entryIndex{};
    std::uint32_t sliceSetIndex{};
    std::uint32_t mapBubbleIndex{};
    std::uint32_t stateHash{};
    std::uint32_t rawU32At12{};
    std::uint32_t entryTag{};
    std::uint32_t registryTag{};
    bool enabled{};
    Text id{};
    Text entryId{};
    Text registryId{};
};

/** One normalized descriptor shape retained to compare repeated object definitions. */
struct DescriptorEvidence final {
    std::uint32_t componentClass{};
    std::uint32_t senseSchema{};
    std::uint32_t authSchema{};

    bool operator==(const DescriptorEvidence&) const = default;
};

/** One canonical definition slot before reflection and mission-policy joins. */
struct Slot final {
    std::uint32_t objectIndex{state::build_data::scriptables::kNoRow};
    std::uint32_t nameHash{};
    std::uint32_t slotIndex{};
    std::uint32_t slotType{};
    std::uint32_t componentClass{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t senseSchema{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t authSchema{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t flags{};
    /** Package descriptor candidates remain separate from the pending reflection join. */
    std::uint32_t descriptorComponentClass{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t descriptorSenseSchema{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t descriptorAuthSchema{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t descriptorCount{state::activity_sdk::format::kAbsentIndex};
    Text id{};
    std::uint32_t idString{state::build_data::scriptables::kNoRow};
    std::uint32_t observedNameRow{state::build_data::scriptables::kNoRow};
    std::uint32_t firstObservedAlias{};
    std::uint32_t observedAliasCount{};
    std::vector<DescriptorEvidence> descriptorEvidence{};
};

/** One object definition deduplicated by tag from scenario-local placement rows. */
struct Object final {
    std::uint32_t objectTag{};
    std::uint32_t objectKey{};
    std::uint32_t firstSlot{};
    std::uint32_t slotCount{};
    std::uint32_t configCount{};
    std::uint32_t placedSubblockCount{};
    std::uint32_t placedLeafCount{};
    std::uint32_t placedHopCount{};
    std::uint32_t bareTargetCount{};
    std::uint32_t replicatedPlacementCount{};
    std::uint32_t descriptorCount{state::activity_sdk::format::kAbsentIndex};
    bool descriptorEvidenceComplete{};
    bool countEvidenceComplete{};
    Text id{};
    std::uint32_t idString{state::build_data::scriptables::kNoRow};
    std::uint32_t pendingCountMask{kObjectCountPendingMask};
    std::vector<Slot> definitionSlots{};
};

/** One exact scenario occurrence before its object definition index is linked. */
struct Occurrence final {
    std::uint32_t scenarioIndex{};
    std::uint32_t bubbleIndex{};
    std::uint32_t stateIndex{};
    std::uint32_t objectIndex{state::build_data::scriptables::kNoRow};
    std::uint32_t objectTag{};
    std::uint32_t registryTag{};
    std::uint32_t entryTag{};
    std::uint32_t registryField{};
    std::uint32_t objectOrdinal{};
    Text id{};
    Text contextRegistryKey{};
    Text registryId{};
    Text entryId{};
    std::uint32_t idString{state::build_data::scriptables::kNoRow};
    std::uint32_t contextRegistryKeyString{state::build_data::scriptables::kNoRow};
    std::uint32_t registryIdString{state::build_data::scriptables::kNoRow};
    std::uint32_t entryIdString{state::build_data::scriptables::kNoRow};
};

/** One exact value in the estate-wide package-inline byte bank. */
struct InlineName final {
    std::uint32_t hash{};
    std::uint32_t firstByte{};
    std::uint32_t byteCount{};
};

/** Mutable indexes used only while scenario topology is being accumulated. */
struct Accumulator final {
    std::unordered_map<std::uint32_t, std::size_t> objectRows{};
    std::unordered_set<std::string> inlineNames{};
    std::size_t inlineNameBytes{};
};

/** All activity rows and exact scenario topology for one installed content identity. */
struct Snapshot final {
    std::vector<Activity> activities{};
    std::vector<Scenario> scenarios{};
    std::vector<Bubble> bubbles{};
    std::vector<State> states{};
    std::vector<Object> objects{};
    std::vector<Occurrence> occurrences{};
    std::vector<Slot> slots{};
    std::vector<InlineName> inlineNames{};
    std::vector<std::byte> inlineNameBytes{};
    std::vector<std::uint32_t> observedAliases{};
    /** Sorted proved strings only; later sections must rebuild final byte offsets. */
    std::vector<std::string> strings{};
    Accumulator accumulator{};
    std::size_t nextScenario{};
    /** Name and final-string closure stay false until the full read universe is proved. */
    bool nameInventoryComplete{};
    bool stringInventoryComplete{};
    bool ready{};
};

/** Starts one deterministic topology build from the complete activity inventory. */
[[nodiscard]] bool begin(const activity_inventory::Snapshot& source, Snapshot& output) noexcept;

/** Appends the next scenario shard and rejects gaps, repeats, and lossy state rows. */
[[nodiscard]] bool append(const state::build_data::scriptables::Snapshot& source,
                          Snapshot& output) noexcept;

/** Closes all scenario ranges without claiming the later exact content join. */
[[nodiscard]] bool finish(Snapshot& output) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::topology_inventory
