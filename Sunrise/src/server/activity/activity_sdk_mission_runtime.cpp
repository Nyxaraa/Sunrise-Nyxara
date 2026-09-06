#include "activity_sdk_mission_runtime.h"

#include <Windows.h>

#include <array>
#include <limits>
#include <span>

#include "../../middleware/bap/activity_message/sensor_auth_update.h"
#include "../../state/activity/runtime.h"
#include "activity_sdk_scriptable_route.h"
#include "activity_sdk_behavior_scope.h"
#include "host_runtime.h"

namespace sunrise::server::activity::activity_sdk_mission {
namespace {

namespace layouts = state::build_data::scenarios;
namespace message = middleware::bap::activity_message::sensor_auth_update;
namespace sdk = state::activity_sdk;
namespace sdk_route = server::activity::activity_sdk_scriptables;

SRWLOCK g_materializeLock{SRWLOCK_INIT};
std::array<layouts::RosterGroup, message::kPublishedGroupCapacity> g_materializedGroups{};

/** Exact private route retained while one authored-scene request is staged. */
struct PreparedScene final {
    host::ScriptableTarget target{};
    layouts::RosterGroup rosterGroup{};
    std::uint64_t activityClientGeneration{};
    std::uint32_t scenarioRow{sdk::format::kAbsentIndex};
    std::uint32_t stateRow{sdk::format::kAbsentIndex};
    std::int32_t effectiveRegion{-1};
};

/** Maps exact SDK binding validation to this facade's stable refusal surface. */
[[nodiscard]] Status binding_status(const sdk::BoundView& view,
                                    server::bap::ActivityLinkView& link) noexcept {
    if (view.catalog == nullptr || sdk::bound_activity(view) == nullptr
        || sdk::bound_scenario(view) == nullptr) {
        return Status::invalidView;
    }
    if (!state::activity::binding_matches(view.binding)) {
        return Status::staleBinding;
    }
    (void)server::bap::activity_link_view(view.binding, link);
    switch (
        sdk::revalidate(view, view.binding, link.matchingLinks, link.activityClientGeneration)) {
    case sdk::Status::ready:
        return Status::ready;
    case sdk::Status::missingClient:
    case sdk::Status::ambiguousClient:
        return Status::noActivityLink;
    case sdk::Status::staleSession:
        return Status::staleBinding;
    case sdk::Status::staleActivityClient:
        return Status::staleActivityClient;
    case sdk::Status::notReady:
    case sdk::Status::missing:
    case sdk::Status::wrongSdkBuild:
    case sdk::Status::catalogInvalid:
    case sdk::Status::wrongActivity:
    case sdk::Status::activityJoinNotExact:
    case sdk::Status::missingScenarioLink:
        return Status::invalidView;
    }
    return Status::invalidView;
}

/** Maps the SDK materializer's closed set of semantic failures. */
[[nodiscard]] Status mission_status(sdk::MissionSeedStatus status) noexcept {
    switch (status) {
    case sdk::MissionSeedStatus::ready:
        return Status::ready;
    case sdk::MissionSeedStatus::invalidView:
        return Status::invalidView;
    case sdk::MissionSeedStatus::invalidSliceSet:
        return Status::missingLiveSliceSet;
    case sdk::MissionSeedStatus::missingInitialState:
        return Status::missingInitialState;
    case sdk::MissionSeedStatus::ambiguousInitialState:
        return Status::ambiguousInitialState;
    case sdk::MissionSeedStatus::invalidOccurrence:
        return Status::invalidOccurrence;
    case sdk::MissionSeedStatus::schemaJoinNotExact:
        return Status::schemaJoinNotExact;
    case sdk::MissionSeedStatus::invalidRosterGroup:
        return Status::invalidRosterGroup;
    case sdk::MissionSeedStatus::rosterKeyConflict:
        return Status::rosterKeyConflict;
    case sdk::MissionSeedStatus::groupCapacityExceeded:
        return Status::groupCapacityExceeded;
    }
    return Status::refused;
}

/** Maps the transport lease's closed set of connection/refusal outcomes. */
[[nodiscard]] Status lease_status(server::bap::ActivityMissionSeedLeaseStatus status) noexcept {
    using LeaseStatus = server::bap::ActivityMissionSeedLeaseStatus;
    switch (status) {
    case LeaseStatus::ready:
        return Status::ready;
    case LeaseStatus::noActivityLink:
        return Status::noActivityLink;
    case LeaseStatus::staleActivityClient:
        return Status::staleActivityClient;
    case LeaseStatus::wrongScenario:
        return Status::wrongScenario;
    case LeaseStatus::missingLiveSliceSet:
        return Status::missingLiveSliceSet;
    case LeaseStatus::wrongSliceSet:
        return Status::wrongSliceSet;
    case LeaseStatus::outputBusy:
        return Status::outputBusy;
    case LeaseStatus::refused:
        return Status::refused;
    }
    return Status::refused;
}

/** @return True when two plans leave the same objects out of their seed, in the same order. */
[[nodiscard]] bool same_omissions(const server::bap::ActivityMissionSeedPlan& left,
                                  const server::bap::ActivityMissionSeedPlan& right) noexcept {
    if (left.omissionCount != right.omissionCount) {
        return false;
    }
    for (std::uint32_t index = 0; index < left.omissionCount; ++index) {
        if (left.omissions[index].objectTag != right.omissions[index].objectTag
            || left.omissions[index].registryKey != right.omissions[index].registryKey) {
            return false;
        }
    }
    return true;
}

/** Copies the immutable scalar materializer output into a transport-owned plan. */
[[nodiscard]] server::bap::ActivityMissionSeedPlan
plan_from(const sdk::MissionSeedSummary& summary) noexcept {
    server::bap::ActivityMissionSeedPlan plan{};
    plan.activityRow = summary.activityRow;
    plan.scenarioRow = summary.scenarioRow;
    plan.stateRow = summary.stateRow;
    plan.bubbleRow = summary.bubbleRow;
    plan.bubbleOrdinal = summary.bubbleOrdinal;
    plan.stateOrdinal = summary.stateOrdinal;
    plan.entryIndex = summary.entryIndex;
    plan.sliceSetIndex = summary.sliceSetIndex;
    plan.effectiveRegion = summary.effectiveRegion;
    plan.omissions = summary.omissions;
    plan.omissionCount = summary.omissionCount;
    plan.occurrenceCount = summary.occurrenceCount;
    plan.groupCount = summary.groupCount;
    plan.authMappingSlots = summary.authMappingSlots;
    plan.authResetSlots = summary.authResetSlots;
    plan.senseSuppressedSlots = summary.senseSuppressedSlots;
    return plan;
}

/** @return True when two plans name the same immutable generated rows and counts. */
[[nodiscard]] bool same_plan(const server::bap::ActivityMissionSeedPlan& left,
                             const server::bap::ActivityMissionSeedPlan& right) noexcept {
    return left.activityRow == right.activityRow && left.scenarioRow == right.scenarioRow
           && left.stateRow == right.stateRow && left.bubbleRow == right.bubbleRow
           && left.bubbleOrdinal == right.bubbleOrdinal && left.stateOrdinal == right.stateOrdinal
           && left.entryIndex == right.entryIndex && left.sliceSetIndex == right.sliceSetIndex
           && left.effectiveRegion == right.effectiveRegion && same_omissions(left, right)
           && left.occurrenceCount == right.occurrenceCount && left.groupCount == right.groupCount
           && left.authMappingSlots == right.authMappingSlots
           && left.authResetSlots == right.authResetSlots
           && left.senseSuppressedSlots == right.senseSuppressedSlots;
}

/** Materializes into static lock-owned storage so UI stack size stays bounded. */
[[nodiscard]] Status materialize_plan(const sdk::BoundView& view,
                                      std::int32_t effectiveRegion,
                                      std::span<const sdk::MissionSeedOmission> omissions,
                                      server::bap::ActivityMissionSeedPlan& output) noexcept {
    output = {};
    if (effectiveRegion < 0) {
        return Status::missingLiveSliceSet;
    }
    sdk::MissionSeedSummary summary{};
    AcquireSRWLockExclusive(&g_materializeLock);
    const sdk::MissionSeedStatus materialized = sdk::materialize_initial_mission_seed(
        view, effectiveRegion, omissions, std::span(g_materializedGroups), summary);
    ReleaseSRWLockExclusive(&g_materializeLock);
    const Status status = mission_status(materialized);
    if (status == Status::ready) {
        output = plan_from(summary);
    }
    return status;
}

/** Reads the transport lease after the binding has been revalidated. */
[[nodiscard]] Status read_lease(const sdk::BoundView& view,
                                const server::bap::ActivityLinkView& link,
                                server::bap::ActivityMissionSeedLeaseView& output) noexcept {
    return lease_status(server::bap::activity_mission_seed_lease(
        view.binding, view.scenarioRow, link.activityClientGeneration, output));
}

/** Maps the shared binding result to the authored-scene refusal surface. */
[[nodiscard]] SceneStatus scene_binding_status(const sdk::BoundView& view,
                                               server::bap::ActivityLinkView& link) noexcept {
    switch (binding_status(view, link)) {
    case Status::ready:
        // A public-target link publishes the state-local groups of the public bubbles it hosts,
        // so a scene, task or cue in one of them is reachable only through that link.
        return link.effectiveRegion >= 0 ? SceneStatus::ready : SceneStatus::noActivityLink;
    case Status::staleBinding:
        return SceneStatus::staleBinding;
    case Status::staleActivityClient:
        return SceneStatus::staleActivityClient;
    case Status::noActivityLink:
        return SceneStatus::noActivityLink;
    case Status::invalidView:
    case Status::missingLiveSliceSet:
    case Status::wrongScenario:
    case Status::wrongSliceSet:
    case Status::missingInitialState:
    case Status::ambiguousInitialState:
    case Status::invalidOccurrence:
    case Status::schemaJoinNotExact:
    case Status::invalidRosterGroup:
    case Status::rosterKeyConflict:
    case Status::groupCapacityExceeded:
    case Status::outputBusy:
    case Status::refused:
        return SceneStatus::invalidView;
    }
    return SceneStatus::invalidView;
}

/** Checks the exact enabled and published mission-seed lease for one scene state. */
[[nodiscard]] SceneStatus scene_lease_status(const sdk::BoundView& view,
                                             const server::bap::ActivityLinkView& link,
                                             std::uint32_t stateRow) noexcept {
    server::bap::ActivityMissionSeedLeaseView lease{};
    switch (server::bap::activity_mission_seed_lease(
        view.binding, view.scenarioRow, link.activityClientGeneration, lease)) {
    case server::bap::ActivityMissionSeedLeaseStatus::ready:
        break;
    case server::bap::ActivityMissionSeedLeaseStatus::noActivityLink:
        return SceneStatus::noActivityLink;
    case server::bap::ActivityMissionSeedLeaseStatus::staleActivityClient:
        return SceneStatus::staleActivityClient;
    case server::bap::ActivityMissionSeedLeaseStatus::outputBusy:
        return SceneStatus::outputBusy;
    case server::bap::ActivityMissionSeedLeaseStatus::wrongScenario:
    case server::bap::ActivityMissionSeedLeaseStatus::missingLiveSliceSet:
    case server::bap::ActivityMissionSeedLeaseStatus::wrongSliceSet:
    case server::bap::ActivityMissionSeedLeaseStatus::refused:
        return SceneStatus::missionSeedUnavailable;
    }
    if (lease.activityClientGeneration != link.activityClientGeneration) {
        return SceneStatus::staleActivityClient;
    }
    if (!lease.configured || lease.revision == 0 || lease.plan.scenarioRow != view.scenarioRow) {
        return SceneStatus::missionSeedUnavailable;
    }
    if (lease.plan.stateRow != stateRow
        && !behavior_scope::live_state(view.catalog->states(), view.catalog->bubbles(),
            view.scenarioRow, stateRow, link.effectiveRegion)) {
        return SceneStatus::wrongState;
    }
    if (lease.publicationPending || lease.publishedRevision != lease.revision) {
        return SceneStatus::missionSeedPending;
    }
    return SceneStatus::ready;
}

/** Resolves one exact generated scene without changing transport state. */
[[nodiscard]] SceneStatus prepare_scene(const sdk::BoundView& view,
                                        std::uint32_t occurrenceRow,
                                        std::uint32_t slotRow,
                                        PreparedScene& output) noexcept {
    output = {};
    server::bap::ActivityLinkView link{};
    const SceneStatus live = scene_binding_status(view, link);
    if (live != SceneStatus::ready) {
        return live;
    }

    const sdk::Catalog& catalog = *view.catalog;
    const sdk::format::Scenario* const scenario = sdk::bound_scenario(view);
    const auto occurrences = catalog.occurrences();
    const auto objects = catalog.objects();
    const auto slots = catalog.slots();
    const auto states = catalog.states();
    const auto bubbles = catalog.bubbles();
    if (scenario == nullptr || occurrenceRow >= occurrences.size()) {
        return SceneStatus::invalidOccurrence;
    }
    if (slotRow >= slots.size()) {
        return SceneStatus::invalidSlot;
    }

    const sdk::format::Occurrence& occurrence = occurrences[occurrenceRow];
    if (occurrence.scenarioIndex != view.scenarioRow) {
        return SceneStatus::wrongScenario;
    }
    if (occurrence.objectIndex >= objects.size() || occurrence.stateIndex >= states.size()
        || occurrence.bubbleIndex >= bubbles.size()) {
        return SceneStatus::invalidOccurrence;
    }
    const sdk::format::Object& object = objects[occurrence.objectIndex];
    const sdk::format::Slot& slot = slots[slotRow];
    const sdk::format::State& state = states[occurrence.stateIndex];
    const sdk::format::Bubble& bubble = bubbles[occurrence.bubbleIndex];
    if (slot.objectIndex != occurrence.objectIndex || slotRow < object.slots.first
        || slotRow - object.slots.first >= object.slots.count) {
        return SceneStatus::invalidSlot;
    }
    if (state.scenarioIndex != view.scenarioRow || bubble.scenarioIndex != view.scenarioRow
        || state.bubbleIndex != occurrence.bubbleIndex) {
        return SceneStatus::invalidOccurrence;
    }
    if (slot.slotType != sdk::format::kAuthoredSceneSlotType
        || slot.componentClass != sdk::format::kAuthoredSceneComponentClass
        || slot.senseSchema != sdk::format::kAuthoredSceneSenseSchema
        || slot.authSchema != sdk::format::kAuthoredSceneAuthSchema) {
        return SceneStatus::invalidSlot;
    }
    if ((slot.flags & sdk::format::kSlotSchemaJoinExact) == 0) {
        return SceneStatus::schemaJoinNotExact;
    }
    if (slot.slotIndex > (std::numeric_limits<std::uint16_t>::max)()
        || slot.slotType > (std::numeric_limits<std::uint8_t>::max)()) {
        return SceneStatus::invalidSlot;
    }

    const auto resources = sdk::slot_authored_scene_resources(catalog, slot);
    if (resources.empty()) {
        return SceneStatus::missingResource;
    }
    if (resources.size() != 1) {
        return SceneStatus::ambiguousResource;
    }
    const sdk::format::AuthoredSceneResource& resource = resources.front();
    if (resource.slotIndex != slotRow || resource.configTag == 0
        || resource.configTag == sdk::format::kAbsentIndex || resource.resourceTag == 0
        || resource.resourceTag == sdk::format::kAbsentIndex
        || resource.resourceClass != sdk::format::kAuthoredSceneResourceClass
        || resource.flags != sdk::format::kAuthoredSceneResourceExact
        || resource.descriptorOffset > (std::numeric_limits<std::uint32_t>::max)()
                                           - sdk::format::kAuthoredSceneResourceRelativeOffset
        || resource.resourceFieldOffset
               != resource.descriptorOffset + sdk::format::kAuthoredSceneResourceRelativeOffset) {
        return SceneStatus::missingResource;
    }

    const SceneStatus lease = scene_lease_status(view, link, occurrence.stateIndex);
    if (lease != SceneStatus::ready) {
        return lease;
    }

    sdk_route::SourceIdentity source{};
    source.scenarioTag = scenario->tag;
    source.objectTag = object.objectTag;
    source.registryKey = object.objectKey;
    source.authSchema = slot.authSchema;
    source.objectRow = occurrence.objectIndex;
    source.stateRow = occurrence.stateIndex;
    source.slotIndex = static_cast<std::uint16_t>(slot.slotIndex);
    source.slotType = static_cast<std::uint8_t>(slot.slotType);

    sdk_route::Resolution resolved{};
    switch (sdk_route::resolve(view, source, link.effectiveRegion, resolved)) {
    case sdk_route::Status::ready:
        break;
    case sdk_route::Status::invalidView:
        return SceneStatus::invalidView;
    case sdk_route::Status::invalidSource:
        return SceneStatus::invalidSlot;
    case sdk_route::Status::missingSource:
        return SceneStatus::targetUnavailable;
    case sdk_route::Status::ambiguousSource:
        return SceneStatus::ambiguousTarget;
    }
    if (resolved.scenarioRow != view.scenarioRow || resolved.stateRow != occurrence.stateIndex
        || resolved.region != link.effectiveRegion || !resolved.target.stateLocalRoster
        || resolved.target.slotType != sdk::format::kAuthoredSceneSlotType
        || resolved.target.authSchema != sdk::format::kAuthoredSceneAuthSchema) {
        return SceneStatus::invalidSlot;
    }

    output.target = resolved.target;
    output.rosterGroup = resolved.rosterGroup;
    output.activityClientGeneration = link.activityClientGeneration;
    output.scenarioRow = resolved.scenarioRow;
    output.stateRow = resolved.stateRow;
    output.effectiveRegion = resolved.region;
    return SceneStatus::ready;
}

/** Resolves one exact SDK-bounded type-53 cue without changing transport state. */
[[nodiscard]] SceneStatus prepare_dialogue(const sdk::BoundView& view,
                                           std::uint32_t occurrenceRow,
                                           std::uint32_t slotRow,
                                           std::uint16_t cueIndex,
                                           PreparedScene& output,
                                           std::uint16_t& authoredCueCount) noexcept {
    output = {};
    authoredCueCount = 0;
    server::bap::ActivityLinkView link{};
    const SceneStatus live = scene_binding_status(view, link);
    if (live != SceneStatus::ready) {
        return live;
    }
    const sdk::Catalog& catalog = *view.catalog;
    const sdk::format::Scenario* const scenario = sdk::bound_scenario(view);
    const auto occurrences = catalog.occurrences();
    const auto objects = catalog.objects();
    const auto slots = catalog.slots();
    const auto states = catalog.states();
    const auto bubbles = catalog.bubbles();
    if (scenario == nullptr || occurrenceRow >= occurrences.size()) {
        return SceneStatus::invalidOccurrence;
    }
    if (slotRow >= slots.size()) {
        return SceneStatus::invalidSlot;
    }
    const sdk::format::Occurrence& occurrence = occurrences[occurrenceRow];
    if (occurrence.scenarioIndex != view.scenarioRow || occurrence.objectIndex >= objects.size()
        || occurrence.stateIndex >= states.size() || occurrence.bubbleIndex >= bubbles.size()) {
        return occurrence.scenarioIndex != view.scenarioRow ? SceneStatus::wrongScenario
                                                            : SceneStatus::invalidOccurrence;
    }
    const sdk::format::Object& object = objects[occurrence.objectIndex];
    const sdk::format::Slot& slot = slots[slotRow];
    const sdk::format::State& state = states[occurrence.stateIndex];
    const sdk::format::Bubble& bubble = bubbles[occurrence.bubbleIndex];
    if (slot.objectIndex != occurrence.objectIndex || slotRow < object.slots.first
        || slotRow - object.slots.first >= object.slots.count
        || state.scenarioIndex != view.scenarioRow || bubble.scenarioIndex != view.scenarioRow
        || state.bubbleIndex != occurrence.bubbleIndex) {
        return SceneStatus::invalidSlot;
    }
    if (slot.slotType != sdk::format::kDialogueSlotType
        || slot.componentClass != sdk::format::kDialogueComponentClass
        || slot.authSchema != sdk::format::kDialogueAuthSchema
        || (slot.flags & (sdk::format::kSlotSchemaJoinExact | sdk::format::kSlotDialogueCuesExact))
               != (sdk::format::kSlotSchemaJoinExact | sdk::format::kSlotDialogueCuesExact)
        || slot.reserved == 0 || slot.reserved > sdk::format::kDialogueMaximumCueCount
        || cueIndex >= slot.reserved) {
        return SceneStatus::invalidSlot;
    }
    authoredCueCount = static_cast<std::uint16_t>(slot.reserved);
    const SceneStatus lease = scene_lease_status(view, link, occurrence.stateIndex);
    if (lease != SceneStatus::ready) {
        return lease;
    }
    sdk_route::SourceIdentity source{};
    source.scenarioTag = scenario->tag;
    source.objectTag = object.objectTag;
    source.registryKey = object.objectKey;
    source.authSchema = slot.authSchema;
    source.objectRow = occurrence.objectIndex;
    source.stateRow = occurrence.stateIndex;
    source.slotIndex = static_cast<std::uint16_t>(slot.slotIndex);
    source.slotType = static_cast<std::uint8_t>(slot.slotType);
    sdk_route::Resolution resolved{};
    switch (sdk_route::resolve(view, source, link.effectiveRegion, resolved)) {
    case sdk_route::Status::ready:
        break;
    case sdk_route::Status::invalidView:
        return SceneStatus::invalidView;
    case sdk_route::Status::invalidSource:
        return SceneStatus::invalidSlot;
    case sdk_route::Status::missingSource:
        return SceneStatus::targetUnavailable;
    case sdk_route::Status::ambiguousSource:
        return SceneStatus::ambiguousTarget;
    }
    if (resolved.scenarioRow != view.scenarioRow || resolved.stateRow != occurrence.stateIndex
        || resolved.region != link.effectiveRegion || !resolved.target.stateLocalRoster
        || resolved.target.slotType != sdk::format::kDialogueSlotType
        || resolved.target.authSchema != sdk::format::kDialogueAuthSchema) {
        return SceneStatus::invalidSlot;
    }
    output.target = resolved.target;
    output.rosterGroup = resolved.rosterGroup;
    output.activityClientGeneration = link.activityClientGeneration;
    output.scenarioRow = resolved.scenarioRow;
    output.stateRow = resolved.stateRow;
    output.effectiveRegion = resolved.region;
    return SceneStatus::ready;
}

/** Resolves one exact fixed-schema behavior slot without changing transport state. */
[[nodiscard]] SceneStatus prepare_typed_behavior(const sdk::BoundView& view,
                                                 std::uint32_t occurrenceRow,
                                                 std::uint32_t slotRow,
                                                 std::uint32_t expectedSlotType,
                                                 std::uint32_t expectedComponentClass,
                                                 std::uint32_t expectedAuthSchema,
                                                 bool requireTaskTarget,
                                                 PreparedScene& output) noexcept {
    output = {};
    server::bap::ActivityLinkView link{};
    const SceneStatus live = scene_binding_status(view, link);
    if (live != SceneStatus::ready) {
        return live;
    }
    const sdk::Catalog& catalog = *view.catalog;
    const sdk::format::Scenario* const scenario = sdk::bound_scenario(view);
    const auto occurrences = catalog.occurrences();
    const auto objects = catalog.objects();
    const auto slots = catalog.slots();
    const auto states = catalog.states();
    const auto bubbles = catalog.bubbles();
    if (scenario == nullptr || occurrenceRow >= occurrences.size()) {
        return SceneStatus::invalidOccurrence;
    }
    if (slotRow >= slots.size()) {
        return SceneStatus::invalidSlot;
    }
    const sdk::format::Occurrence& occurrence = occurrences[occurrenceRow];
    if (occurrence.scenarioIndex != view.scenarioRow || occurrence.objectIndex >= objects.size()
        || occurrence.stateIndex >= states.size() || occurrence.bubbleIndex >= bubbles.size()) {
        return occurrence.scenarioIndex != view.scenarioRow ? SceneStatus::wrongScenario
                                                            : SceneStatus::invalidOccurrence;
    }
    const sdk::format::Object& object = objects[occurrence.objectIndex];
    const sdk::format::Slot& slot = slots[slotRow];
    const sdk::format::State& state = states[occurrence.stateIndex];
    const sdk::format::Bubble& bubble = bubbles[occurrence.bubbleIndex];
    if (slot.objectIndex != occurrence.objectIndex || slotRow < object.slots.first
        || slotRow - object.slots.first >= object.slots.count
        || state.scenarioIndex != view.scenarioRow || bubble.scenarioIndex != view.scenarioRow
        || state.bubbleIndex != occurrence.bubbleIndex) {
        return SceneStatus::invalidSlot;
    }
    if (slot.slotType != expectedSlotType || slot.componentClass != expectedComponentClass
        || slot.authSchema != expectedAuthSchema
        || (slot.flags & sdk::format::kSlotSchemaJoinExact) == 0
        || (requireTaskTarget && sdk::slot_task_targets(catalog, slot).empty())
        || slot.slotIndex > (std::numeric_limits<std::uint16_t>::max)()) {
        return SceneStatus::invalidSlot;
    }
    const SceneStatus lease = scene_lease_status(view, link, occurrence.stateIndex);
    if (lease != SceneStatus::ready) {
        return lease;
    }
    sdk_route::SourceIdentity source{};
    source.scenarioTag = scenario->tag;
    source.objectTag = object.objectTag;
    source.registryKey = object.objectKey;
    source.authSchema = slot.authSchema;
    source.objectRow = occurrence.objectIndex;
    source.stateRow = occurrence.stateIndex;
    source.slotIndex = static_cast<std::uint16_t>(slot.slotIndex);
    source.slotType = static_cast<std::uint8_t>(slot.slotType);
    sdk_route::Resolution resolved{};
    switch (sdk_route::resolve(view, source, link.effectiveRegion, resolved)) {
    case sdk_route::Status::ready:
        break;
    case sdk_route::Status::invalidView:
        return SceneStatus::invalidView;
    case sdk_route::Status::invalidSource:
        return SceneStatus::invalidSlot;
    case sdk_route::Status::missingSource:
        return SceneStatus::targetUnavailable;
    case sdk_route::Status::ambiguousSource:
        return SceneStatus::ambiguousTarget;
    }
    if (resolved.scenarioRow != view.scenarioRow || resolved.stateRow != occurrence.stateIndex
        || resolved.region != link.effectiveRegion || !resolved.target.stateLocalRoster
        || resolved.target.slotType != expectedSlotType
        || resolved.target.authSchema != expectedAuthSchema) {
        return SceneStatus::invalidSlot;
    }
    output.target = resolved.target;
    output.rosterGroup = resolved.rosterGroup;
    output.activityClientGeneration = link.activityClientGeneration;
    output.scenarioRow = resolved.scenarioRow;
    output.stateRow = resolved.stateRow;
    output.effectiveRegion = resolved.region;
    return SceneStatus::ready;
}

/** Resolves one exact SDK-linked type-38 task without changing transport state. */
[[nodiscard]] SceneStatus prepare_task(const sdk::BoundView& view,
                                       std::uint32_t occurrenceRow,
                                       std::uint32_t slotRow,
                                       PreparedScene& output) noexcept {
    return prepare_typed_behavior(view,
                                  occurrenceRow,
                                  slotRow,
                                  sdk::format::kTaskSlotType,
                                  sdk::format::kTaskComponentClass,
                                  sdk::format::kTaskAuthSchema,
                                  true,
                                  output);
}

/** Resolves one exact type-68 HUD state without changing transport state. */
[[nodiscard]] SceneStatus prepare_directive(const sdk::BoundView& view,
                                            std::uint32_t occurrenceRow,
                                            std::uint32_t slotRow,
                                            std::uint32_t nameHash,
                                            std::int32_t elementIndex,
                                            PreparedScene& output) noexcept {
    const SceneStatus status = prepare_typed_behavior(view,
                                                      occurrenceRow,
                                                      slotRow,
                                                      sdk::format::kDirectiveSlotType,
                                                      sdk::format::kDirectiveComponentClass,
                                                      sdk::format::kDirectiveAuthSchema,
                                                      false,
                                                      output);
    if (status != SceneStatus::ready) {
        return status;
    }
    std::size_t matches = 0;
    for (const sdk::format::DirectiveElement& row : view.catalog->directive_elements()) {
        matches +=
            row.slotIndex == slotRow && row.nameHash == nameHash && row.elementIndex == elementIndex
                ? 1U
                : 0U;
    }
    return matches == 1 ? SceneStatus::ready : SceneStatus::invalidSlot;
}

/** Resolves one exact type-3 objective sensor without changing transport state. */
[[nodiscard]] SceneStatus prepare_objective(const sdk::BoundView& view,
                                            std::uint32_t occurrenceRow,
                                            std::uint32_t slotRow,
                                            PreparedScene& output) noexcept {
    return prepare_typed_behavior(view,
                                  occurrenceRow,
                                  slotRow,
                                  sdk::format::kObjectiveSlotType,
                                  sdk::format::kObjectiveComponentClass,
                                  sdk::format::kObjectiveAuthSchema,
                                  false,
                                  output);
}

} // namespace

/** Resolves the generated plan and current lease without changing transport state. */
Status query(const sdk::BoundView& view, Snapshot& output) noexcept {
    output = {};
    server::bap::ActivityLinkView link{};
    const Status binding = binding_status(view, link);
    if (binding != Status::ready) {
        return binding;
    }
    output.activityClientGeneration = link.activityClientGeneration;
    output.arrivalSliceSetIndex = link.arrivalSliceSetIndex;
    output.liveSliceSetIndex = link.sliceSetIndex;
    output.effectiveRegion = link.effectiveRegion;

    server::bap::ActivityMissionSeedLeaseView lease{};
    const Status leaseResult = read_lease(view, link, lease);
    if (leaseResult != Status::ready) {
        return leaseResult;
    }
    output.revision = lease.revision;
    output.publishedRevision = lease.publishedRevision;
    output.configured = lease.configured;
    output.publicationPending = lease.publicationPending;
    output.regionArrivalPending = lease.regionArrivalPending;

    server::bap::ActivityMissionSeedPlan generated{};
    const std::int32_t selectedRegion =
        lease.configured
                && lease.plan.effectiveRegion
                       <= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())
            ? static_cast<std::int32_t>(lease.plan.effectiveRegion)
            : link.effectiveRegion;
    // The lease's own omissions produced its plan, so the check must re-materialize with them.
    const Status generatedStatus = materialize_plan(
        view,
        selectedRegion,
        std::span(lease.plan.omissions).first(lease.configured ? lease.plan.omissionCount : 0),
        generated);
    output.plan = lease.configured ? lease.plan : generated;
    if (generatedStatus != Status::ready) {
        return generatedStatus;
    }
    if (lease.configured && !same_plan(lease.plan, generated)) {
        return lease.plan.effectiveRegion == generated.effectiveRegion ? Status::refused
                                                                       : Status::wrongSliceSet;
    }
    return Status::ready;
}

/** Selects one exact authored state and exposes its asynchronous publication state. */
Status select_state(const sdk::BoundView& view,
                    std::int32_t effectiveRegion,
                    std::span<const sdk::MissionSeedOmission> omissions,
                    Snapshot& output) noexcept {
    output = {};
    server::bap::ActivityLinkView link{};
    const Status binding = binding_status(view, link);
    if (binding != Status::ready) {
        return binding;
    }
    server::bap::ActivityMissionSeedPlan plan{};
    const Status materialized = materialize_plan(view, effectiveRegion, omissions, plan);
    if (materialized != Status::ready) {
        return materialized;
    }
    const Status selected = lease_status(server::bap::select_activity_mission_seed(
        view.binding, plan, link.activityClientGeneration));
    if (selected != Status::ready) {
        return selected;
    }
    return query(view, output);
}

/** Checks one exact occurrence and type-43 slot without changing transport state. */
SceneStatus authored_scene_availability(const sdk::BoundView& view,
                                        std::uint32_t occurrenceRow,
                                        std::uint32_t slotRow) noexcept {
    PreparedScene prepared{};
    return prepare_scene(view, occurrenceRow, slotRow, prepared);
}

/** Queues the next generation for one exact state-local authored scene. */
SceneStatus activate_authored_scene(const sdk::BoundView& view,
                                    std::uint32_t occurrenceRow,
                                    std::uint32_t slotRow) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_scene(view, occurrenceRow, slotRow, prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    if (server::bap::request_activity_state_local_authored_scene_override(
            view.binding,
            prepared.target,
            prepared.rosterGroup,
            prepared.effectiveRegion,
            prepared.activityClientGeneration,
            prepared.scenarioRow,
            prepared.stateRow)) {
        return SceneStatus::queued;
    }
    return SceneStatus::refused;
}

/** Checks one exact SDK-bounded authored dialogue cue. */
SceneStatus dialogue_cue_availability(const sdk::BoundView& view,
                                      std::uint32_t occurrenceRow,
                                      std::uint32_t slotRow,
                                      std::uint16_t cueIndex) noexcept {
    PreparedScene prepared{};
    std::uint16_t authoredCueCount = 0;
    return prepare_dialogue(view, occurrenceRow, slotRow, cueIndex, prepared, authoredCueCount);
}

/** Queues one authored dialogue cue through the dedicated type-53 encoder. */
SceneStatus play_dialogue_cue(const sdk::BoundView& view,
                              std::uint32_t occurrenceRow,
                              std::uint32_t slotRow,
                              std::uint16_t cueIndex) noexcept {
    PreparedScene prepared{};
    std::uint16_t authoredCueCount = 0;
    const SceneStatus status =
        prepare_dialogue(view, occurrenceRow, slotRow, cueIndex, prepared, authoredCueCount);
    if (status != SceneStatus::ready) {
        return status;
    }
    if (server::bap::request_activity_state_local_dialogue_override(
            view.binding,
            prepared.target,
            prepared.rosterGroup,
            cueIndex,
            authoredCueCount,
            prepared.effectiveRegion,
            prepared.activityClientGeneration,
            prepared.scenarioRow,
            prepared.stateRow)) {
        return SceneStatus::queued;
    }
    return SceneStatus::refused;
}

SceneStatus directive_availability(const sdk::BoundView& view,
                                   std::uint32_t occurrenceRow,
                                   std::uint32_t slotRow,
                                   std::uint32_t nameHash,
                                   std::int32_t elementIndex) noexcept {
    PreparedScene prepared{};
    return prepare_directive(view, occurrenceRow, slotRow, nameHash, elementIndex, prepared);
}

/** Sets one HUD directive element's state and visibility on a prepared objective slot. */
SceneStatus set_directive(const sdk::BoundView& view,
                          std::uint32_t occurrenceRow,
                          std::uint32_t slotRow,
                          std::uint32_t nameHash,
                          std::int32_t elementIndex,
                          std::int8_t state,
                          bool visible) noexcept {
    PreparedScene prepared{};
    const SceneStatus status =
        prepare_directive(view, occurrenceRow, slotRow, nameHash, elementIndex, prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    middleware::bap::activity_message::scriptable_auth::Type68Preset preset{
        .nameHash = nameHash, .elementIndex = elementIndex, .state = state, .visible = visible};
    std::array<std::byte, middleware::bap::activity_message::scriptable_auth::kType68ByteCount>
        body{};
    std::size_t written = 0;
    if (!middleware::bap::activity_message::scriptable_auth::encode_type68(preset, body, written)
        || written != body.size()) {
        return SceneStatus::invalidSlot;
    }
    return server::bap::request_activity_sdk_auth_override(
               view.binding,
               prepared.target,
               &prepared.rosterGroup,
               body,
               middleware::bap::activity_message::scriptable_auth::kType68BitCount,
               prepared.effectiveRegion,
               prepared.activityClientGeneration,
               prepared.scenarioRow,
               prepared.stateRow)
               ? SceneStatus::queued
               : SceneStatus::refused;
}

[[nodiscard]] SceneStatus
play_dialogue_cue_reserved(const sdk::BoundView& view,
                           std::uint32_t occurrenceRow,
                           std::uint32_t slotRow,
                           std::uint16_t cueIndex,
                           const host::ScriptableOutputReservation& reservation) noexcept {
    PreparedScene prepared{};
    std::uint16_t authoredCueCount = 0;
    const SceneStatus status =
        prepare_dialogue(view, occurrenceRow, slotRow, cueIndex, prepared, authoredCueCount);
    if (status != SceneStatus::ready) {
        return status;
    }
    return server::bap::request_activity_state_local_dialogue_override(
               view.binding,
               prepared.target,
               prepared.rosterGroup,
               cueIndex,
               authoredCueCount,
               prepared.effectiveRegion,
               prepared.activityClientGeneration,
               prepared.scenarioRow,
               prepared.stateRow,
               &reservation)
               ? SceneStatus::queued
               : SceneStatus::refused;
}

/** Checks one exact SDK-linked authored task. */
SceneStatus task_availability(const sdk::BoundView& view,
                              std::uint32_t occurrenceRow,
                              std::uint32_t slotRow) noexcept {
    PreparedScene prepared{};
    return prepare_task(view, occurrenceRow, slotRow, prepared);
}

SceneStatus objective_reset_availability(const sdk::BoundView& view,
                                         std::uint32_t occurrenceRow,
                                         std::uint32_t slotRow) noexcept {
    PreparedScene prepared{};
    return prepare_objective(view, occurrenceRow, slotRow, prepared);
}

/** Clears every objective element of one prepared objective slot. */
SceneStatus reset_objectives(const sdk::BoundView& view,
                             std::uint32_t occurrenceRow,
                             std::uint32_t slotRow) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_objective(view, occurrenceRow, slotRow, prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    if (server::bap::request_activity_state_local_objective_reset(view.binding,
                                                                  prepared.target,
                                                                  prepared.rosterGroup,
                                                                  prepared.effectiveRegion,
                                                                  prepared.activityClientGeneration,
                                                                  prepared.scenarioRow,
                                                                  prepared.stateRow)) {
        return SceneStatus::queued;
    }
    return SceneStatus::refused;
}

/** Queues one exact objective reset using a caller-owned output reservation. */
[[nodiscard]] SceneStatus
reset_objectives_reserved(const sdk::BoundView& view,
                          std::uint32_t occurrenceRow,
                          std::uint32_t slotRow,
                          const host::ScriptableOutputReservation& reservation) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_objective(view, occurrenceRow, slotRow, prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    return server::bap::request_activity_state_local_objective_reset(
               view.binding,
               prepared.target,
               prepared.rosterGroup,
               prepared.effectiveRegion,
               prepared.activityClientGeneration,
               prepared.scenarioRow,
               prepared.stateRow,
               &reservation)
               ? SceneStatus::queued
               : SceneStatus::refused;
}

/** Queues the next generation through the dedicated type-38 encoder. */
SceneStatus activate_task(const sdk::BoundView& view,
                          std::uint32_t occurrenceRow,
                          std::uint32_t slotRow) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_task(view, occurrenceRow, slotRow, prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    if (server::bap::request_activity_state_local_task_override(view.binding,
                                                                prepared.target,
                                                                prepared.rosterGroup,
                                                                prepared.effectiveRegion,
                                                                prepared.activityClientGeneration,
                                                                prepared.scenarioRow,
                                                                prepared.stateRow)) {
        return SceneStatus::queued;
    }
    return SceneStatus::refused;
}

[[nodiscard]] SceneStatus
activate_task_reserved(const sdk::BoundView& view,
                       std::uint32_t occurrenceRow,
                       std::uint32_t slotRow,
                       const host::ScriptableOutputReservation& reservation) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_task(view, occurrenceRow, slotRow, prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    return server::bap::request_activity_state_local_task_override(
               view.binding,
               prepared.target,
               prepared.rosterGroup,
               prepared.effectiveRegion,
               prepared.activityClientGeneration,
               prepared.scenarioRow,
               prepared.stateRow,
               &reservation)
               ? SceneStatus::queued
               : SceneStatus::refused;
}

/** @return Whether one occurrence's sequence slot can be played right now. */
SceneStatus sequence_availability(const sdk::BoundView& view,
                                  std::uint32_t occurrenceRow,
                                  std::uint32_t slotRow) noexcept {
    PreparedScene prepared{};
    return prepare_typed_behavior(view,
                                  occurrenceRow,
                                  slotRow,
                                  sdk::format::kSequenceSlotType,
                                  sdk::format::kSequenceComponentClass,
                                  sdk::format::kSequenceAuthSchema,
                                  false,
                                  prepared);
}

/** Plays one sequence slot of a prepared behavior occurrence. */
SceneStatus play_sequence(const sdk::BoundView& view,
                          std::uint32_t occurrenceRow,
                          std::uint32_t slotRow) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_typed_behavior(view,
                                                      occurrenceRow,
                                                      slotRow,
                                                      sdk::format::kSequenceSlotType,
                                                      sdk::format::kSequenceComponentClass,
                                                      sdk::format::kSequenceAuthSchema,
                                                      false,
                                                      prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    if (server::bap::request_activity_state_local_sequence_override(
            view.binding,
            prepared.target,
            prepared.rosterGroup,
            prepared.effectiveRegion,
            prepared.activityClientGeneration,
            prepared.scenarioRow,
            prepared.stateRow)) {
        return SceneStatus::queued;
    }
    return SceneStatus::refused;
}

/** @return Whether one occurrence's cinematic slot can be driven right now. */
SceneStatus cinematic_availability(const sdk::BoundView& view,
                                   std::uint32_t occurrenceRow,
                                   std::uint32_t slotRow) noexcept {
    PreparedScene prepared{};
    return prepare_typed_behavior(view,
                                  occurrenceRow,
                                  slotRow,
                                  sdk::format::kCinematicSlotType,
                                  sdk::format::kCinematicComponentClass,
                                  sdk::format::kCinematicAuthSchema,
                                  false,
                                  prepared);
}

/** Sets one cinematic slot active or inactive on a prepared behavior occurrence. */
SceneStatus set_cinematic_active(const sdk::BoundView& view,
                                 std::uint32_t occurrenceRow,
                                 std::uint32_t slotRow,
                                 bool active) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_typed_behavior(view,
                                                      occurrenceRow,
                                                      slotRow,
                                                      sdk::format::kCinematicSlotType,
                                                      sdk::format::kCinematicComponentClass,
                                                      sdk::format::kCinematicAuthSchema,
                                                      false,
                                                      prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    if (server::bap::request_activity_state_local_cinematic_override(
            view.binding,
            prepared.target,
            prepared.rosterGroup,
            active,
            prepared.effectiveRegion,
            prepared.activityClientGeneration,
            prepared.scenarioRow,
            prepared.stateRow)) {
        return SceneStatus::queued;
    }
    return SceneStatus::refused;
}

/** Plays one sequence slot against an already-reserved Host output revision. */
SceneStatus play_sequence_reserved(const sdk::BoundView& view,
                                   std::uint32_t occurrenceRow,
                                   std::uint32_t slotRow,
                                   const host::ScriptableOutputReservation& reservation) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_typed_behavior(view,
                                                      occurrenceRow,
                                                      slotRow,
                                                      sdk::format::kSequenceSlotType,
                                                      sdk::format::kSequenceComponentClass,
                                                      sdk::format::kSequenceAuthSchema,
                                                      false,
                                                      prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    return server::bap::request_activity_state_local_sequence_override(
               view.binding,
               prepared.target,
               prepared.rosterGroup,
               prepared.effectiveRegion,
               prepared.activityClientGeneration,
               prepared.scenarioRow,
               prepared.stateRow,
               &reservation)
               ? SceneStatus::queued
               : SceneStatus::refused;
}

SceneStatus
set_cinematic_active_reserved(const sdk::BoundView& view,
                              std::uint32_t occurrenceRow,
                              std::uint32_t slotRow,
                              bool active,
                              const host::ScriptableOutputReservation& reservation) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_typed_behavior(view,
                                                      occurrenceRow,
                                                      slotRow,
                                                      sdk::format::kCinematicSlotType,
                                                      sdk::format::kCinematicComponentClass,
                                                      sdk::format::kCinematicAuthSchema,
                                                      false,
                                                      prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    return server::bap::request_activity_state_local_cinematic_override(
               view.binding,
               prepared.target,
               prepared.rosterGroup,
               active,
               prepared.effectiveRegion,
               prepared.activityClientGeneration,
               prepared.scenarioRow,
               prepared.stateRow,
               &reservation)
               ? SceneStatus::queued
               : SceneStatus::refused;
}

/** Finds the occurrence a slot currently belongs to. @return `invalidView` on a stale view. */
[[nodiscard]] static SceneStatus current_behavior_occurrence(
    const sdk::BoundView& view, std::uint32_t slotRow, std::uint32_t& occurrenceRow) noexcept {
    occurrenceRow = sdk::format::kAbsentIndex;
    Snapshot snapshot{};
    if (query(view, snapshot) != Status::ready || view.catalog == nullptr) {
        return SceneStatus::invalidView;
    }
    const auto slots = view.catalog->slots();
    const auto occurrences = view.catalog->occurrences();
    if (slotRow >= slots.size()) {
        return SceneStatus::invalidSlot;
    }
    server::bap::ActivityLinkView link{};
    const SceneStatus live = scene_binding_status(view, link);
    if (live != SceneStatus::ready) return live;
    const auto selected = behavior_scope::select(occurrences, view.catalog->states(),
        view.catalog->bubbles(), view.scenarioRow, slots[slotRow].objectIndex,
        snapshot.plan.stateRow, link.effectiveRegion);
    if (selected.ambiguous) return SceneStatus::ambiguousTarget;
    occurrenceRow = selected.row;
    return occurrenceRow == sdk::format::kAbsentIndex ? SceneStatus::targetUnavailable
                                                      : SceneStatus::ready;
}

SceneStatus sequence_slot_availability(const sdk::BoundView& view, std::uint32_t slotRow) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready ? sequence_availability(view, occurrenceRow, slotRow)
                                       : found;
}

SceneStatus objective_reset_slot_availability(const sdk::BoundView& view,
                                              std::uint32_t slotRow) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready ? objective_reset_availability(view, occurrenceRow, slotRow)
                                       : found;
}

SceneStatus
reset_objectives_slot_reserved(const sdk::BoundView& view,
                               std::uint32_t slotRow,
                               const host::ScriptableOutputReservation& reservation) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready
               ? reset_objectives_reserved(view, occurrenceRow, slotRow, reservation)
               : found;
}

SceneStatus
play_sequence_slot_reserved(const sdk::BoundView& view,
                            std::uint32_t slotRow,
                            const host::ScriptableOutputReservation& reservation) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready
               ? play_sequence_reserved(view, occurrenceRow, slotRow, reservation)
               : found;
}

SceneStatus cinematic_slot_availability(const sdk::BoundView& view,
                                        std::uint32_t slotRow) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready ? cinematic_availability(view, occurrenceRow, slotRow)
                                       : found;
}

SceneStatus
set_cinematic_slot_active_reserved(const sdk::BoundView& view,
                                   std::uint32_t slotRow,
                                   bool active,
                                   const host::ScriptableOutputReservation& reservation) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready
               ? set_cinematic_active_reserved(view, occurrenceRow, slotRow, active, reservation)
               : found;
}

/** Starts one named state of the actor a type-42 sensor drives, behind its rising generation. */
[[nodiscard]] static SceneStatus
play_performance_reserved(const sdk::BoundView& view,
                          std::uint32_t occurrenceRow,
                          std::uint32_t slotRow,
                          std::uint32_t stateNameHash,
                          const host::ScriptableOutputReservation& reservation) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_typed_behavior(view,
                                                      occurrenceRow,
                                                      slotRow,
                                                      sdk::format::kPerformanceSlotType,
                                                      sdk::format::kPerformanceComponentClass,
                                                      sdk::format::kPerformanceAuthSchema,
                                                      false,
                                                      prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    return server::bap::request_activity_state_local_performance_override(
               view.binding,
               prepared.target,
               prepared.rosterGroup,
               stateNameHash,
               prepared.effectiveRegion,
               prepared.activityClientGeneration,
               prepared.scenarioRow,
               prepared.stateRow,
               &reservation)
               ? SceneStatus::queued
               : SceneStatus::refused;
}

/** Starts one state on the squad the sensor drives, through the slot's current occurrence. */
SceneStatus
play_performance_slot_reserved(const sdk::BoundView& view,
                               std::uint32_t slotRow,
                               std::uint32_t stateNameHash,
                               const host::ScriptableOutputReservation& reservation) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready
               ? play_performance_reserved(view, occurrenceRow, slotRow, stateNameHash, reservation)
               : found;
}

/** Starts one authored state on the squad a type-42 sensor drives, for an operator action. */
SceneStatus play_performance_slot(const sdk::BoundView& view,
                                  std::uint32_t slotRow,
                                  std::uint32_t stateNameHash) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    if (found != SceneStatus::ready) {
        return found;
    }
    PreparedScene prepared{};
    const SceneStatus status = prepare_typed_behavior(view,
                                                      occurrenceRow,
                                                      slotRow,
                                                      sdk::format::kPerformanceSlotType,
                                                      sdk::format::kPerformanceComponentClass,
                                                      sdk::format::kPerformanceAuthSchema,
                                                      false,
                                                      prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    return server::bap::request_activity_state_local_performance_override(
               view.binding,
               prepared.target,
               prepared.rosterGroup,
               stateNameHash,
               prepared.effectiveRegion,
               prepared.activityClientGeneration,
               prepared.scenarioRow,
               prepared.stateRow)
               ? SceneStatus::queued
               : SceneStatus::refused;
}

SceneStatus task_slot_availability(const sdk::BoundView& view, std::uint32_t slotRow) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready ? task_availability(view, occurrenceRow, slotRow) : found;
}

SceneStatus
activate_task_slot_reserved(const sdk::BoundView& view,
                            std::uint32_t slotRow,
                            const host::ScriptableOutputReservation& reservation) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready
               ? activate_task_reserved(view, occurrenceRow, slotRow, reservation)
               : found;
}

SceneStatus dialogue_cue_slot_availability(const sdk::BoundView& view,
                                           std::uint32_t slotRow,
                                           std::uint16_t cueIndex) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready
               ? dialogue_cue_availability(view, occurrenceRow, slotRow, cueIndex)
               : found;
}

SceneStatus
play_dialogue_cue_slot_reserved(const sdk::BoundView& view,
                                std::uint32_t slotRow,
                                std::uint16_t cueIndex,
                                const host::ScriptableOutputReservation& reservation) noexcept {
    std::uint32_t occurrenceRow = 0;
    const SceneStatus found = current_behavior_occurrence(view, slotRow, occurrenceRow);
    return found == SceneStatus::ready
               ? play_dialogue_cue_reserved(view, occurrenceRow, slotRow, cueIndex, reservation)
               : found;
}

/** Queues one preflighted authored scene only through an exact unarmed Host revision. */
SceneStatus
activate_authored_scene_reserved(const sdk::BoundView& view,
                                 std::uint32_t occurrenceRow,
                                 std::uint32_t slotRow,
                                 const host::ScriptableOutputReservation& reservation) noexcept {
    PreparedScene prepared{};
    const SceneStatus status = prepare_scene(view, occurrenceRow, slotRow, prepared);
    if (status != SceneStatus::ready) {
        return status;
    }
    if (server::bap::request_activity_state_local_authored_scene_override(
            view.binding,
            prepared.target,
            prepared.rosterGroup,
            prepared.effectiveRegion,
            prepared.activityClientGeneration,
            prepared.scenarioRow,
            prepared.stateRow,
            &reservation)) {
        return SceneStatus::queued;
    }
    return SceneStatus::refused;
}

/** Returns stable concise text for one mission runtime result. */
const char* status_name(Status status) noexcept {
    switch (status) {
    case Status::ready:
        return "ready";
    case Status::invalidView:
        return "invalid_view";
    case Status::staleBinding:
        return "stale_binding";
    case Status::staleActivityClient:
        return "stale_activity_client";
    case Status::noActivityLink:
        return "no_activity_link";
    case Status::missingLiveSliceSet:
        return "missing_live_slice_set";
    case Status::wrongScenario:
        return "wrong_scenario";
    case Status::wrongSliceSet:
        return "wrong_slice_set";
    case Status::missingInitialState:
        return "missing_initial_state";
    case Status::ambiguousInitialState:
        return "ambiguous_initial_state";
    case Status::invalidOccurrence:
        return "invalid_occurrence";
    case Status::schemaJoinNotExact:
        return "schema_join_not_exact";
    case Status::invalidRosterGroup:
        return "invalid_roster_group";
    case Status::rosterKeyConflict:
        return "roster_key_conflict";
    case Status::groupCapacityExceeded:
        return "group_capacity_exceeded";
    case Status::outputBusy:
        return "output_busy";
    case Status::refused:
        return "refused";
    }
    return "refused";
}

/** Returns stable concise text for one authored-scene result. */
const char* status_name(SceneStatus status) noexcept {
    switch (status) {
    case SceneStatus::ready:
        return "ready";
    case SceneStatus::queued:
        return "queued";
    case SceneStatus::invalidView:
        return "invalid_view";
    case SceneStatus::staleBinding:
        return "stale_binding";
    case SceneStatus::staleActivityClient:
        return "stale_activity_client";
    case SceneStatus::noActivityLink:
        return "no_activity_link";
    case SceneStatus::invalidOccurrence:
        return "invalid_occurrence";
    case SceneStatus::invalidSlot:
        return "invalid_slot";
    case SceneStatus::wrongScenario:
        return "wrong_scenario";
    case SceneStatus::wrongState:
        return "wrong_state";
    case SceneStatus::schemaJoinNotExact:
        return "schema_join_not_exact";
    case SceneStatus::missingResource:
        return "missing_resource";
    case SceneStatus::ambiguousResource:
        return "ambiguous_resource";
    case SceneStatus::targetUnavailable:
        return "target_unavailable";
    case SceneStatus::ambiguousTarget:
        return "ambiguous_target";
    case SceneStatus::missionSeedUnavailable:
        return "mission_seed_unavailable";
    case SceneStatus::missionSeedPending:
        return "mission_seed_pending";
    case SceneStatus::outputBusy:
        return "output_busy";
    case SceneStatus::refused:
        return "refused";
    }
    return "refused";
}

} // namespace sunrise::server::activity::activity_sdk_mission
