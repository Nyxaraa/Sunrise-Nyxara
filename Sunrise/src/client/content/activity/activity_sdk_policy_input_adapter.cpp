#include "activity_sdk_policy_input_adapter.h"

#include <string_view>
#include <utility>

#include "activity_sdk_policy_inventory_internal.h"

namespace sunrise::client::content::activity::sdk_generation::policy_input_adapter {
namespace {

namespace format = state::activity_sdk::format;
namespace catalog = state::build_data::scriptables;
namespace internal = policy_inventory::internal;

/**
 * Resolves one owned topology value only while its bounded UTF-8 shape is valid.
 * @param source The fixed native text storage.
 * @param output Receives a view into source.
 * @return True when the retained length, terminator, and UTF-8 are valid.
 */
[[nodiscard]] bool text_view(const topology_inventory::Text& source,
                             std::string_view& output) noexcept {
    output = {};
    if (source.length >= source.value.size() || source.value[source.length] != '\0') {
        return false;
    }
    output = std::string_view(source.value.data(), source.length);
    return internal::valid_text(output);
}

/**
 * Converts the exhaustive activity join outcome without relying on enum values.
 * @param source The native activity inventory outcome.
 * @param output Receives the policy outcome.
 * @return True when source names a supported outcome.
 */
[[nodiscard]] bool convert_join(activity_inventory::JoinStatus source,
                                policy::ActivityJoinStatus& output) noexcept {
    switch (source) {
    case activity_inventory::JoinStatus::exact:
        output = policy::ActivityJoinStatus::exact;
        return true;
    case activity_inventory::JoinStatus::sourceNameMissing:
        output = policy::ActivityJoinStatus::sourceNameMissing;
        return true;
    case activity_inventory::JoinStatus::liveNameMissing:
        output = policy::ActivityJoinStatus::liveNameMissing;
        return true;
    case activity_inventory::JoinStatus::liveNameAmbiguous:
        output = policy::ActivityJoinStatus::liveNameAmbiguous;
        return true;
    }
    return false;
}

/**
 * Adds final activity rows while cross-checking post-enrichment topology state.
 * @param activities The source of definition identity and join outcomes.
 * @param topology The final activity rows.
 * @param output Receives borrowed policy rows.
 * @return True when row order, names, hashes, joins, and flags agree.
 */
[[nodiscard]] bool build_activities(const activity_inventory::Snapshot& activities,
                                    const topology_inventory::Snapshot& topology,
                                    Snapshot& output) {
    if (activities.activities.size() != topology.activities.size()) {
        return false;
    }
    output.activities.reserve(topology.activities.size());
    for (std::size_t index = 0; index < topology.activities.size(); ++index) {
        const activity_inventory::ActivityVariant& source = activities.activities[index];
        const topology_inventory::Activity& final = topology.activities[index];
        std::string_view id{};
        std::string_view internalName{};
        std::string_view displayName{};
        policy::ActivityJoinStatus join{};
        const bool joined = source.joinStatus == activity_inventory::JoinStatus::exact;
        if (source.definition.internalNameLength >= source.definition.internalName.size()
            || source.definition.internalName[source.definition.internalNameLength] != '\0'
            || source.definition.activityIndex != index || final.activityIndex != index
            || source.definition.definitionHash != final.definitionHash || !text_view(final.id, id)
            || !text_view(final.internalName, internalName)
            || !text_view(final.displayName, displayName) || !convert_join(source.joinStatus, join)
            || internalName
                   != std::string_view(source.definition.internalName.data(),
                                       source.definition.internalNameLength)
            || (joined
                && (final.scenarioIndex >= topology.scenarios.size()
                    || topology.scenarios[final.scenarioIndex].tag != source.scenarioTag
                    || final.exactFlags != format::kActivityExactMask))
            || (!joined && (final.scenarioIndex != catalog::kNoRow || final.exactFlags != 0))) {
            return false;
        }
        output.activities.push_back(
            {static_cast<std::uint32_t>(index), id, internalName, displayName, join});
    }
    return true;
}

/**
 * Adds final slots and expands enrichment-local aliases to global slot references.
 * @param topology The final base slot order and identities.
 * @param enrichment The validated slot names, schemas, flags, and aliases.
 * @param output Receives borrowed policy rows.
 * @return True when every slot and alias range closes exactly.
 */
[[nodiscard]] bool build_slots(const topology_inventory::Snapshot& topology,
                               const topology_enrichment::Snapshot& enrichment,
                               Snapshot& output) {
    if (topology.slots.size() != enrichment.slots.size()) {
        return false;
    }
    output.slots.reserve(topology.slots.size());
    output.slotAliases.reserve(enrichment.slotAliases.size());
    std::size_t nextAlias = 0;
    for (std::size_t index = 0; index < topology.slots.size(); ++index) {
        const topology_inventory::Slot& source = topology.slots[index];
        const topology_enrichment::Slot& final = enrichment.slots[index];
        std::string_view id{};
        std::string_view name{};
        std::string_view senseSchemaId{};
        std::string_view authSchemaId{};
        if (source.objectIndex >= topology.objects.size()
            || source.componentClass != format::kAbsentIndex
            || source.senseSchema != format::kAbsentIndex
            || source.authSchema != format::kAbsentIndex || source.flags != 0
            || (final.flags & ~format::kSlotFlagMask) != 0 || final.aliases.first != nextAlias
            || final.aliases.count > enrichment.slotAliases.size() - nextAlias
            || !text_view(source.id, id) || !text_view(final.name, name)
            || !text_view(final.senseSchemaId, senseSchemaId)
            || !text_view(final.authSchemaId, authSchemaId)) {
            return false;
        }
        output.slots.push_back({source.objectIndex,
                                source.slotIndex,
                                source.slotType,
                                final.componentClass,
                                final.senseSchema,
                                final.authSchema,
                                id,
                                name,
                                senseSchemaId,
                                authSchemaId,
                                policy::ExtractionStatus::complete,
                                final.flags});
        for (std::uint32_t ordinal = 0; ordinal < final.aliases.count; ++ordinal) {
            std::string_view alias{};
            if (!text_view(enrichment.slotAliases[nextAlias + ordinal], alias)) {
                return false;
            }
            output.slotAliases.push_back({static_cast<std::uint32_t>(index), alias});
        }
        nextAlias += final.aliases.count;
    }
    return nextAlias == enrichment.slotAliases.size();
}

/**
 * Adds occurrence bindings in canonical topology row order.
 * @param topology The closed source topology.
 * @param output Receives compact policy bindings.
 * @return True when every occurrence names valid parent rows.
 */
[[nodiscard]] bool build_occurrences(const topology_inventory::Snapshot& topology,
                                     Snapshot& output) {
    output.occurrences.reserve(topology.occurrences.size());
    for (const topology_inventory::Occurrence& source : topology.occurrences) {
        if (source.scenarioIndex >= topology.scenarios.size()
            || source.objectIndex >= topology.objects.size()) {
            return false;
        }
        output.occurrences.push_back({source.scenarioIndex, source.objectIndex});
    }
    return true;
}

} // namespace

policy::Inputs Snapshot::view() const noexcept {
    return {activities,
            slots,
            slotAliases,
            occurrences,
            policy::host_surfaces(),
            scenarioCount,
            objectCount};
}

/**
 * Adapts closed activity/topology rows and validated topology enrichment transactionally.
 * @param activities The final source of activity join outcomes.
 * @param topology The closed topology after activity enrichment was applied.
 * @param enrichment The validated final slot names, schemas, flags, and local alias ranges.
 * @param output Receives borrowed input rows only after every cross-check succeeds.
 * @return True when row order, final flags, text, and child ranges close exactly.
 */
bool build(const activity_inventory::Snapshot& activities,
           const topology_inventory::Snapshot& topology,
           const topology_enrichment::Snapshot& enrichment,
           Snapshot& output) noexcept {
    if (!topology.ready || topology.nameInventoryComplete || topology.stringInventoryComplete
        || topology.activities.size() > policy::kMaximumActivityCount
        || topology.scenarios.size() > policy::kMaximumScenarioCount
        || topology.objects.size() > policy::kMaximumObjectCount
        || topology.occurrences.size() > policy::kMaximumOccurrenceCount
        || topology.slots.size() > policy::kMaximumSlotCount
        || enrichment.slotAliases.size() > policy::kMaximumPolicyTextCount) {
        return false;
    }
    try {
        Snapshot pending{};
        pending.scenarioCount = static_cast<std::uint32_t>(topology.scenarios.size());
        pending.objectCount = static_cast<std::uint32_t>(topology.objects.size());
        if (!build_activities(activities, topology, pending)
            || !build_slots(topology, enrichment, pending) || !build_occurrences(topology, pending)
            || !internal::valid_inputs(pending.view())) {
            return false;
        }
        output = std::move(pending);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace sunrise::client::content::activity::sdk_generation::policy_input_adapter
