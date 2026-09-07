#include "mission_script_catalog_sdk_bridge.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>

#include "mission_script_sdk_bridge.h"

namespace sunrise::server::activity::mission::sdk_bridge {
namespace {

namespace sdk = state::activity_sdk;
namespace format = state::activity_sdk::format;

template <std::size_t Size>
[[nodiscard]] bool same_digest(std::span<const std::byte> value,
                               const std::array<std::byte, Size>& expected) noexcept {
    return value.size() == expected.size()
           && std::equal(value.begin(), value.end(), expected.begin());
}

/** @return True when the view still matches the generation the handle was minted under. */
[[nodiscard]] bool same_generation(const sdk::BoundView& view,
                                   const lua_vm::CatalogGenerationIdentity& expected) noexcept {
    const format::Activity* const activity = sdk::bound_activity(view);
    const format::Scenario* const scenario = sdk::bound_scenario(view);
    return view.catalog != nullptr && activity != nullptr && scenario != nullptr
           && same_digest(view.catalog->sdk_build_sha256(), expected.sdkBuildSha256)
           && same_digest(view.catalog->payload_sha256(), expected.sdkPayloadSha256)
           && same_digest(view.catalog->content_key_sha256(), expected.contentKeySha256)
           && same_digest(view.catalog->logical_ir_sha256(), expected.logicalIrSha256)
           && view.activityClientGeneration == expected.activityClientGeneration
           && view.activityRow == expected.activityRow && view.scenarioRow == expected.scenarioRow
           && activity->definitionHash == expected.definitionHash
           && scenario->tag == expected.scenarioTag;
}

[[nodiscard]] const sdk::BoundView*
checked_view(const void* context, const lua_vm::CatalogGenerationIdentity& generation) noexcept {
    const auto* const view = static_cast<const sdk::BoundView*>(context);
    return view != nullptr && same_generation(*view, generation) ? view : nullptr;
}

[[nodiscard]] bool validate_catalog(const void* context,
                                    const lua_vm::CatalogGenerationIdentity& generation) noexcept {
    return checked_view(context, generation) != nullptr;
}

[[nodiscard]] bool field_u32(lua_vm::CatalogFieldDefinition& output, std::uint32_t value) noexcept {
    output = {};
    output.kind = lua_vm::CatalogFieldKind::unsignedInteger;
    output.unsignedValue = value;
    return true;
}

[[nodiscard]] bool field_i32(lua_vm::CatalogFieldDefinition& output, std::int32_t value) noexcept {
    output = {};
    output.kind = lua_vm::CatalogFieldKind::signedInteger;
    output.signedValue = value;
    return true;
}

[[nodiscard]] bool field_u64(lua_vm::CatalogFieldDefinition& output, std::uint64_t value) noexcept {
    output = {};
    output.kind = lua_vm::CatalogFieldKind::unsignedDecimalString;
    output.unsignedValue = value;
    return true;
}

[[nodiscard]] bool field_i64(lua_vm::CatalogFieldDefinition& output, std::int64_t value) noexcept {
    output = {};
    output.kind = lua_vm::CatalogFieldKind::signedDecimalString;
    output.signedValue = value;
    return true;
}

/** Fills one catalog field with borrowed text. @return False when the text is absent. */
[[nodiscard]] bool field_string(lua_vm::CatalogFieldDefinition& output,
                                const sdk::Catalog& catalog,
                                format::StringRef reference) noexcept {
    if (reference.offset == format::kAbsentIndex && reference.length == 0) {
        output = {};
        output.kind = lua_vm::CatalogFieldKind::absent;
        return true;
    }
    const auto bytes = catalog.string_bytes();
    if (reference.offset > bytes.size() || reference.length > bytes.size() - reference.offset) {
        return false;
    }
    output = {};
    output.kind = lua_vm::CatalogFieldKind::string;
    output.stringValue = catalog.string(reference);
    return output.stringValue.size() == reference.length;
}

template <std::size_t Size>
[[nodiscard]] bool field_bytes(lua_vm::CatalogFieldDefinition& output,
                               const std::array<std::byte, Size>& value) noexcept {
    static_assert(Size <= lua_vm::kCatalogFieldByteCapacity);
    output = {};
    output.kind = lua_vm::CatalogFieldKind::bytes;
    std::copy(value.begin(), value.end(), output.bytesValue.begin());
    output.valueCount = static_cast<std::uint8_t>(Size);
    return true;
}

/** @return Row count of one catalog collection kind, or 0 when the generation is stale. */
[[nodiscard]] std::size_t catalog_count(const void* context,
                                        const lua_vm::CatalogGenerationIdentity& generation,
                                        lua_vm::CatalogCollectionKind kind) noexcept {
    const sdk::BoundView* const view = checked_view(context, generation);
    if (view == nullptr) {
        return 0;
    }
    const sdk::Catalog& catalog = *view->catalog;
    switch (kind) {
    case lua_vm::CatalogCollectionKind::activities:
        return catalog.activities().size();
    case lua_vm::CatalogCollectionKind::scenarios:
        return catalog.scenarios().size();
    case lua_vm::CatalogCollectionKind::bubbles:
        return catalog.bubbles().size();
    case lua_vm::CatalogCollectionKind::states:
        return catalog.states().size();
    case lua_vm::CatalogCollectionKind::objects:
        return catalog.objects().size();
    case lua_vm::CatalogCollectionKind::occurrences:
        return catalog.occurrences().size();
    case lua_vm::CatalogCollectionKind::slots:
        return catalog.slots().size();
    case lua_vm::CatalogCollectionKind::texts:
        return catalog.texts().size();
    case lua_vm::CatalogCollectionKind::capabilities:
        return catalog.capabilities().size();
    case lua_vm::CatalogCollectionKind::gates:
        return catalog.gates().size();
    case lua_vm::CatalogCollectionKind::refusals:
        return catalog.refusals().size();
    case lua_vm::CatalogCollectionKind::actorClasses:
        return catalog.actor_classes().size();
    case lua_vm::CatalogCollectionKind::rsatDescriptors:
        return catalog.rsat_descriptors().size();
    case lua_vm::CatalogCollectionKind::rsatSchemas:
        return catalog.rsat_schemas().size();
    case lua_vm::CatalogCollectionKind::rsatFields:
        return catalog.rsat_fields().size();
    case lua_vm::CatalogCollectionKind::squads:
        return catalog.squads().size();
    case lua_vm::CatalogCollectionKind::squadMembers:
        return catalog.squad_members().size();
    case lua_vm::CatalogCollectionKind::squadAnchors:
        return catalog.squad_anchors().size();
    case lua_vm::CatalogCollectionKind::authoredSceneResources:
        return catalog.authored_scene_resources().size();
    case lua_vm::CatalogCollectionKind::authoredSceneSquadEdges:
        return catalog.authored_scene_squad_edges().size();
    case lua_vm::CatalogCollectionKind::activityBindingLocators:
        return catalog.activity_binding_locators().size();
    }
    return 0;
}

template <typename Row>
[[nodiscard]] const Row* selected_row(std::span<const Row> values,
                                      std::uint32_t localRow) noexcept {
    return localRow == 0 || localRow > values.size() ? nullptr : &values[localRow - 1U];
}

/** Resolves every retained runtime-pack field without exposing mapped addresses. */
[[nodiscard]] bool resolve_catalog_field(const void* context,
                                         const lua_vm::CatalogGenerationIdentity& generation,
                                         lua_vm::CatalogCollectionKind kind,
                                         std::uint32_t localRow,
                                         std::string_view key,
                                         lua_vm::CatalogFieldDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = checked_view(context, generation);
    if (view == nullptr || localRow == 0 || localRow > catalog_count(context, generation, kind)) {
        return false;
    }
    const sdk::Catalog& catalog = *view->catalog;
    if (key == "row") {
        return field_u32(output, localRow);
    }

#include "mission_script_catalog_sdk_fields.inc"

    switch (kind) {
    case lua_vm::CatalogCollectionKind::activities: {
        const format::Activity& row = *selected_row(catalog.activities(), localRow);
        lua_vm::ActivityBindingDefinition binding{};
        if (!activity_binding_definition(catalog, row, binding)) {
            return false;
        }
        CATALOG_U32("activity_index", row.activityIndex)
        CATALOG_U32("definition_hash", row.definitionHash)
        CATALOG_STRING("id", row.id)
        CATALOG_STRING("internal_name", row.internalName)
        CATALOG_STRING("display_name", row.displayName)
        CATALOG_U32("scenario_index", row.scenarioIndex)
        CATALOG_U32("flags", row.flags)
        CATALOG_RANGE("aliases", row.aliases)
        CATALOG_RANGE("capabilities", row.capabilities)
        CATALOG_U32("selected_activity_root_tag", row.selectedActivityRootTag)
        CATALOG_U32("selected_scenario_tag", row.selectedScenarioTag)
        CATALOG_U32("matchmaking_config_tag", row.matchmakingConfigTag)
        CATALOG_U32("join_status", row.joinStatus)
        CATALOG_U32("binding_disposition", row.bindingDisposition)
        CATALOG_U32("binding_reason", row.bindingReason)
        CATALOG_U32("binding_evidence_basis", row.bindingEvidenceBasis)
        CATALOG_U32("runnable_status", row.runnableStatus)
        CATALOG_U32("binding_flags", row.bindingFlags)
        CATALOG_RANGE("activity_root_candidate_tags", row.activityRootCandidateTags)
        CATALOG_RANGE("scenario_name_candidate_tags", row.scenarioNameCandidateTags)
        CATALOG_RANGE("evidence_root_tags", row.evidenceRootTags)
        CATALOG_RANGE("binding_locators", row.bindingLocators)
        break;
    }
    case lua_vm::CatalogCollectionKind::scenarios: {
        const format::Scenario& row = *selected_row(catalog.scenarios(), localRow);
        CATALOG_U32("tag", row.tag)
        CATALOG_U32("reserved", row.reserved)
        CATALOG_STRING("id", row.id)
        CATALOG_STRING("name", row.name)
        CATALOG_RANGE("bubbles", row.bubbles)
        CATALOG_RANGE("states", row.states)
        CATALOG_RANGE("occurrences", row.occurrences)
        break;
    }
    case lua_vm::CatalogCollectionKind::bubbles: {
        const format::Bubble& row = *selected_row(catalog.bubbles(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_STRING("name", row.name)
        CATALOG_U32("scenario_index", row.scenarioIndex)
        CATALOG_U32("bubble_ordinal", row.bubbleOrdinal)
        CATALOG_U32("name_hash", row.nameHash)
        CATALOG_U32("reserved", row.reserved)
        CATALOG_RANGE("states", row.states)
        break;
    }
    case lua_vm::CatalogCollectionKind::states: {
        const format::State& row = *selected_row(catalog.states(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_STRING("entry_id", row.entryId)
        CATALOG_STRING("registry_id", row.registryId)
        CATALOG_U32("scenario_index", row.scenarioIndex)
        CATALOG_U32("bubble_index", row.bubbleIndex)
        CATALOG_U32("state_ordinal", row.stateOrdinal)
        CATALOG_U32("entry_index", row.entryIndex)
        CATALOG_U32("slice_set_index", row.sliceSetIndex)
        CATALOG_U32("map_bubble_index", row.mapBubbleIndex)
        CATALOG_U32("state_hash", row.stateHash)
        CATALOG_U32("public_value", row.publicValue)
        CATALOG_U32("flags", row.flags)
        CATALOG_U32("registry_tag", row.registryTag)
        break;
    }
    case lua_vm::CatalogCollectionKind::objects: {
        const format::Object& row = *selected_row(catalog.objects(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_U32("object_tag", row.objectTag)
        CATALOG_U32("object_key", row.objectKey)
        CATALOG_RANGE("slots", row.slots)
        CATALOG_U32("config_count", row.configCount)
        CATALOG_U32("descriptor_count", row.descriptorCount)
        CATALOG_U32("placed_subblock_count", row.placedSubblockCount)
        CATALOG_U32("placed_leaf_count", row.placedLeafCount)
        CATALOG_U32("placed_hop_count", row.placedHopCount)
        CATALOG_U32("bare_target_count", row.bareTargetCount)
        CATALOG_U32("replicated_placement_count", row.replicatedPlacementCount)
        break;
    }
    case lua_vm::CatalogCollectionKind::occurrences: {
        const format::Occurrence& row = *selected_row(catalog.occurrences(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_STRING("context_registry_key", row.contextRegistryKey)
        CATALOG_STRING("registry_id", row.registryId)
        CATALOG_STRING("entry_id", row.entryId)
        CATALOG_U32("scenario_index", row.scenarioIndex)
        CATALOG_U32("bubble_index", row.bubbleIndex)
        CATALOG_U32("state_index", row.stateIndex)
        CATALOG_U32("object_index", row.objectIndex)
        CATALOG_U32("registry_field", row.registryField)
        CATALOG_U32("object_ordinal", row.objectOrdinal)
        break;
    }
    case lua_vm::CatalogCollectionKind::slots: {
        const format::Slot& row = *selected_row(catalog.slots(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_STRING("name", row.name)
        CATALOG_STRING("sense_schema_id", row.senseSchemaId)
        CATALOG_STRING("auth_schema_id", row.authSchemaId)
        CATALOG_U32("object_index", row.objectIndex)
        CATALOG_U32("slot_index", row.slotIndex)
        CATALOG_U32("slot_type", row.slotType)
        CATALOG_U32("component_class", row.componentClass)
        CATALOG_U32("sense_schema", row.senseSchema)
        CATALOG_U32("auth_schema", row.authSchema)
        CATALOG_U32("flags", row.flags)
        CATALOG_U32("reserved", row.reserved)
        CATALOG_RANGE("aliases", row.aliases)
        CATALOG_RANGE("capabilities", row.capabilities)
        break;
    }
    case lua_vm::CatalogCollectionKind::texts: {
        const format::Text& row = *selected_row(catalog.texts(), localRow);
        CATALOG_STRING("value", row.value)
        CATALOG_U32("kind", row.kind)
        CATALOG_U32("reserved", row.reserved)
        break;
    }
    case lua_vm::CatalogCollectionKind::capabilities: {
        const format::Capability& row = *selected_row(catalog.capabilities(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_STRING("operation", row.operation)
        CATALOG_STRING("value_schema_id", row.valueSchemaId)
        CATALOG_U32("subject_kind", row.subjectKind)
        CATALOG_U32("subject_index", row.subjectIndex)
        CATALOG_U32("exposure_flags", row.exposureFlags)
        CATALOG_U32("candidate_exposure_flags", row.candidateExposureFlags)
        CATALOG_RANGE("gates", row.gates)
        CATALOG_RANGE("refusals", row.refusals)
        break;
    }
    case lua_vm::CatalogCollectionKind::gates: {
        const format::Gate& row = *selected_row(catalog.gates(), localRow);
        CATALOG_STRING("gate", row.gate)
        CATALOG_STRING("status", row.status)
        CATALOG_STRING("reason_code", row.reasonCode)
        CATALOG_STRING("required", row.required)
        CATALOG_STRING("observed", row.observed)
        CATALOG_STRING("would_confirm", row.wouldConfirm)
        break;
    }
    case lua_vm::CatalogCollectionKind::refusals: {
        const format::Refusal& row = *selected_row(catalog.refusals(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_STRING("exposure", row.exposure)
        CATALOG_STRING("status", row.status)
        CATALOG_RANGE("reason_codes", row.reasonCodes)
        CATALOG_U32("capability_index", row.capabilityIndex)
        CATALOG_U32("reserved", row.reserved)
        break;
    }
    case lua_vm::CatalogCollectionKind::actorClasses: {
        const format::ActorClass& row = *selected_row(catalog.actor_classes(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_U32("definition_tag", row.definitionTag)
        CATALOG_U32("name_hash", row.nameHash)
        CATALOG_U32("rsat_tag", row.rsatTag)
        CATALOG_U32("rsat_reverse_definition_tag", row.rsatReverseDefinitionTag)
        CATALOG_U32("object_type", row.objectType)
        CATALOG_U32("descriptor_array_offset", row.descriptorArrayOffset)
        CATALOG_I64("descriptor_array_relative", row.descriptorArrayRelative)
        CATALOG_U32("descriptor_array_header_offset", row.descriptorArrayHeaderOffset)
        CATALOG_U32("descriptor_array_data_offset", row.descriptorArrayDataOffset)
        CATALOG_U32("descriptor_element_class", row.descriptorElementClass)
        CATALOG_RANGE("descriptors", row.descriptors)
        CATALOG_U32("dynamic_presence_tail_count", row.dynamicPresenceTailCount)
        CATALOG_U32("authored_profile_0", static_cast<std::uint8_t>(row.authoredSpawnProfile[0]))
        CATALOG_U32("authored_profile_1", static_cast<std::uint8_t>(row.authoredSpawnProfile[1]))
        CATALOG_U32("authored_profile_2", static_cast<std::uint8_t>(row.authoredSpawnProfile[2]))
        CATALOG_U32("authored_profile_3", static_cast<std::uint8_t>(row.authoredSpawnProfile[3]))
        break;
    }
    case lua_vm::CatalogCollectionKind::rsatDescriptors: {
        const format::RsatDescriptor& row = *selected_row(catalog.rsat_descriptors(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_U32("actor_class_index", row.actorClassIndex)
        CATALOG_U32("rsat_tag", row.rsatTag)
        CATALOG_U32("descriptor_ordinal", row.descriptorOrdinal)
        CATALOG_U32("descriptor_offset", row.descriptorOffset)
        CATALOG_U32("descriptor_element_class", row.descriptorElementClass)
        CATALOG_U32("component_tag", row.componentTag)
        CATALOG_U32("schema_index", row.schemaIndex)
        CATALOG_U32("schema_tag", row.schemaTag)
        CATALOG_U32("schema_field_count", row.schemaFieldCount)
        CATALOG_U32("schema_first_field_runtime_gate", row.schemaFirstFieldRuntimeGate)
        CATALOG_U32("schema_first_field_raw_u32_at_10", row.schemaFirstFieldRawU32At10)
        CATALOG_U32("flags", row.flags)
        CATALOG_U32("dynamic_presence_tail_ordinal", row.dynamicPresenceTailOrdinal)
        CATALOG_BYTES("raw_row", row.rawRow)
        break;
    }
    case lua_vm::CatalogCollectionKind::rsatSchemas: {
        const format::RsatSchema& row = *selected_row(catalog.rsat_schemas(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_U32("schema_tag", row.schemaTag)
        CATALOG_U32("schema_class", row.schemaClass)
        CATALOG_U32("field_count", row.fieldCount)
        CATALOG_U32("field_array_offset", row.fieldArrayOffset)
        CATALOG_I64("field_array_relative", row.fieldArrayRelative)
        CATALOG_U32("field_array_header_offset", row.fieldArrayHeaderOffset)
        CATALOG_U32("field_array_data_offset", row.fieldArrayDataOffset)
        CATALOG_U32("field_element_class", row.fieldElementClass)
        CATALOG_U32("first_field_runtime_gate", row.firstFieldRuntimeGate)
        CATALOG_U32("first_field_raw_u32_at_10", row.firstFieldRawU32At10)
        CATALOG_U32("flags", row.flags)
        CATALOG_RANGE("fields", row.fields)
        break;
    }
    case lua_vm::CatalogCollectionKind::rsatFields: {
        const format::RsatField& row = *selected_row(catalog.rsat_fields(), localRow);
        CATALOG_BYTES("raw_row", row.rawRow)
        break;
    }
    case lua_vm::CatalogCollectionKind::squads: {
        const format::Squad& row = *selected_row(catalog.squads(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_U32("scenario_index", row.scenarioIndex)
        CATALOG_U32("object_index", row.objectIndex)
        CATALOG_U32("slot_index", row.slotIndex)
        CATALOG_U32("spawner_config_tag", row.spawnerConfigTag)
        CATALOG_U32("spawn_rule_config_tag", row.spawnRuleConfigTag)
        CATALOG_U32("flags", row.flags)
        CATALOG_U32("occurrence_index", row.occurrenceIndex)
        CATALOG_RANGE("members", row.members)
        CATALOG_RANGE("anchors", row.anchors)
        break;
    }
    case lua_vm::CatalogCollectionKind::squadMembers: {
        const format::SquadMember& row = *selected_row(catalog.squad_members(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_U32("squad_index", row.squadIndex)
        CATALOG_U32("member_ordinal", row.memberOrdinal)
        CATALOG_U32("member_key", row.memberKey)
        CATALOG_U32("actor_class_index", row.actorClassIndex)
        CATALOG_U32("flags", row.flags)
        CATALOG_U32("candidate_count_0", row.candidateCounts[0])
        CATALOG_U32("candidate_count_1", row.candidateCounts[1])
        CATALOG_U32("candidate_count_2", row.candidateCounts[2])
        CATALOG_U32("candidate_count_3", row.candidateCounts[3])
        CATALOG_U32("candidate_count_4", row.candidateCounts[4])
        CATALOG_U32("candidate_count_5", row.candidateCounts[5])
        CATALOG_I32("default_count", row.defaultCount)
        break;
    }
    case lua_vm::CatalogCollectionKind::squadAnchors: {
        const format::SquadAnchor& row = *selected_row(catalog.squad_anchors(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_U32("squad_index", row.squadIndex)
        CATALOG_U32("point_ordinal", row.pointOrdinal)
        CATALOG_U32("object_list_tag", row.objectListTag)
        CATALOG_U32("placement_ordinal", row.placementOrdinal)
        CATALOG_U32("flags", row.flags)
        CATALOG_U64("placed_entry_identity", row.placedEntryIdentity)
        CATALOG_U32("position_bits_x", row.positionBits[0])
        CATALOG_U32("position_bits_y", row.positionBits[1])
        CATALOG_U32("position_bits_z", row.positionBits[2])
        break;
    }
    case lua_vm::CatalogCollectionKind::authoredSceneResources: {
        const format::AuthoredSceneResource& row =
            *selected_row(catalog.authored_scene_resources(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_U32("slot_index", row.slotIndex)
        CATALOG_U32("config_tag", row.configTag)
        CATALOG_U32("descriptor_offset", row.descriptorOffset)
        CATALOG_U32("resource_field_offset", row.resourceFieldOffset)
        CATALOG_U32("resource_tag", row.resourceTag)
        CATALOG_U32("resource_class", row.resourceClass)
        CATALOG_U32("flags", row.flags)
        CATALOG_U32("reserved", row.reserved)
        break;
    }
    case lua_vm::CatalogCollectionKind::authoredSceneSquadEdges: {
        const format::AuthoredSceneSquadEdge& row =
            *selected_row(catalog.authored_scene_squad_edges(), localRow);
        CATALOG_STRING("id", row.id)
        CATALOG_U32("scene_slot_index", row.sceneSlotIndex)
        CATALOG_U32("squad_slot_index", row.squadSlotIndex)
        CATALOG_U32("config_tag", row.configTag)
        CATALOG_U32("descriptor_offset", row.descriptorOffset)
        CATALOG_U32("reference_field_offset", row.referenceFieldOffset)
        CATALOG_U32("target_object_key", row.targetObjectKey)
        CATALOG_U32("flags", row.flags)
        CATALOG_U32("reserved", row.reserved)
        break;
    }
    case lua_vm::CatalogCollectionKind::activityBindingLocators: {
        const format::ActivityBindingLocator& row =
            *selected_row(catalog.activity_binding_locators(), localRow);
        CATALOG_U32("tag", row.tag)
        CATALOG_U32("reserved", row.reserved)
        CATALOG_U64("offset", row.offset)
        break;
    }
    }

#undef CATALOG_BYTES
#undef CATALOG_RANGE
#undef CATALOG_STRING
#undef CATALOG_I64
#undef CATALOG_U64
#undef CATALOG_I32
#undef CATALOG_U32

    output = {};
    output.kind = lua_vm::CatalogFieldKind::absent;
    return true;
}

/** @return The activity range this owned kind names, or null when the kind is unknown. */
[[nodiscard]] const format::Range*
activity_owned_range(const format::Activity& activity,
                     lua_vm::CatalogActivityOwnedKind kind) noexcept {
    switch (kind) {
    case lua_vm::CatalogActivityOwnedKind::activityRootCandidateTags:
        return &activity.activityRootCandidateTags;
    case lua_vm::CatalogActivityOwnedKind::scenarioNameCandidateTags:
        return &activity.scenarioNameCandidateTags;
    case lua_vm::CatalogActivityOwnedKind::evidenceRootTags:
        return &activity.evidenceRootTags;
    case lua_vm::CatalogActivityOwnedKind::bindingLocators:
        return &activity.bindingLocators;
    }
    return nullptr;
}

/**
 * Bounds-checks one owned activity range against the section it indexes.
 * @param output Receives the range; cleared when the generation is stale or the range is invalid.
 * @return False when the row, the kind or the range does not fit the bound catalog.
 */
[[nodiscard]] bool
resolve_activity_owned_range(const void* context,
                             const lua_vm::CatalogGenerationIdentity& generation,
                             std::uint32_t activityRow,
                             lua_vm::CatalogActivityOwnedKind kind,
                             lua_vm::CatalogOwnedRangeDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = checked_view(context, generation);
    if (view == nullptr) {
        return false;
    }
    const format::Activity* const activity = selected_row(view->catalog->activities(), activityRow);
    const format::Range* const range =
        activity == nullptr ? nullptr : activity_owned_range(*activity, kind);
    if (range == nullptr) {
        return false;
    }
    const std::size_t sectionCount = kind == lua_vm::CatalogActivityOwnedKind::bindingLocators
                                         ? view->catalog->activity_binding_locators().size()
                                         : view->catalog->activity_binding_tags().size();
    if (range->first > sectionCount || range->count > sectionCount - range->first) {
        return false;
    }
    if (kind != lua_vm::CatalogActivityOwnedKind::bindingLocators) {
        lua_vm::ActivityBindingTagKind sharedKind{};
        switch (kind) {
        case lua_vm::CatalogActivityOwnedKind::activityRootCandidateTags:
            sharedKind = lua_vm::ActivityBindingTagKind::activityRootCandidates;
            break;
        case lua_vm::CatalogActivityOwnedKind::scenarioNameCandidateTags:
            sharedKind = lua_vm::ActivityBindingTagKind::scenarioNameCandidates;
            break;
        case lua_vm::CatalogActivityOwnedKind::evidenceRootTags:
            sharedKind = lua_vm::ActivityBindingTagKind::evidenceRoots;
            break;
        case lua_vm::CatalogActivityOwnedKind::bindingLocators:
            return false;
        }
        const auto tags = activity_binding_tags(*view->catalog, *activity, sharedKind);
        if (tags.size() != range->count
            || (!tags.empty()
                && tags.data() != view->catalog->activity_binding_tags().data() + range->first)) {
            return false;
        }
    }
    output.firstIndex = range->first;
    output.count = range->count;
    return true;
}

/** Resolves the package tag owned by one 1-based activity row. */
[[nodiscard]] bool resolve_activity_owned_tag(const void* context,
                                              const lua_vm::CatalogGenerationIdentity& generation,
                                              std::uint32_t activityRow,
                                              lua_vm::CatalogActivityOwnedKind kind,
                                              std::uint32_t localRow,
                                              std::uint32_t& output) noexcept {
    output = 0;
    if (kind == lua_vm::CatalogActivityOwnedKind::bindingLocators || localRow == 0) {
        return false;
    }
    const sdk::BoundView* const view = checked_view(context, generation);
    const format::Activity* const activity =
        view == nullptr ? nullptr : selected_row(view->catalog->activities(), activityRow);
    lua_vm::ActivityBindingTagKind sharedKind{};
    switch (kind) {
    case lua_vm::CatalogActivityOwnedKind::activityRootCandidateTags:
        sharedKind = lua_vm::ActivityBindingTagKind::activityRootCandidates;
        break;
    case lua_vm::CatalogActivityOwnedKind::scenarioNameCandidateTags:
        sharedKind = lua_vm::ActivityBindingTagKind::scenarioNameCandidates;
        break;
    case lua_vm::CatalogActivityOwnedKind::evidenceRootTags:
        sharedKind = lua_vm::ActivityBindingTagKind::evidenceRoots;
        break;
    case lua_vm::CatalogActivityOwnedKind::bindingLocators:
        return false;
    }
    if (activity == nullptr) {
        return false;
    }
    const auto tags = activity_binding_tags(*view->catalog, *activity, sharedKind);
    if (localRow > tags.size()) {
        return false;
    }
    output = tags[localRow - 1U].tag;
    return true;
}

} // namespace

/** Builds the identity that pins a catalog view. @return False when the view is not bound. */
bool catalog_generation_identity(const sdk::BoundView& view,
                                 lua_vm::CatalogGenerationIdentity& output) noexcept {
    output = {};
    const format::Activity* const activity = sdk::bound_activity(view);
    const format::Scenario* const scenario = sdk::bound_scenario(view);
    if (view.catalog == nullptr || activity == nullptr || scenario == nullptr) {
        return false;
    }
    const auto copy_digest = [](std::span<const std::byte> source,
                                std::array<std::byte, 32>& destination) noexcept {
        if (source.size() != destination.size()) {
            return false;
        }
        std::copy(source.begin(), source.end(), destination.begin());
        return true;
    };
    if (!copy_digest(view.catalog->sdk_build_sha256(), output.sdkBuildSha256)
        || !copy_digest(view.catalog->payload_sha256(), output.sdkPayloadSha256)
        || !copy_digest(view.catalog->content_key_sha256(), output.contentKeySha256)
        || !copy_digest(view.catalog->logical_ir_sha256(), output.logicalIrSha256)) {
        output = {};
        return false;
    }
    output.activityClientGeneration = view.activityClientGeneration;
    output.activityRow = view.activityRow;
    output.scenarioRow = view.scenarioRow;
    output.definitionHash = activity->definitionHash;
    output.scenarioTag = scenario->tag;
    return output.sdkBuildSha256 != std::array<std::byte, 32>{}
           && output.sdkPayloadSha256 != std::array<std::byte, 32>{}
           && output.contentKeySha256 != std::array<std::byte, 32>{}
           && output.logicalIrSha256 != std::array<std::byte, 32>{};
}

/** Builds the catalog definition API bound to one view. */
lua_vm::CatalogDefinitionApi catalog_definition_api(const sdk::BoundView& view) noexcept {
    lua_vm::CatalogGenerationIdentity generation{};
    if (!catalog_generation_identity(view, generation)) {
        return {};
    }
    return {
        .context = &view,
        .generation = generation,
        .validate = &validate_catalog,
        .count = &catalog_count,
        .resolveField = &resolve_catalog_field,
        .resolveActivityOwnedRange = &resolve_activity_owned_range,
        .resolveActivityOwnedTag = &resolve_activity_owned_tag,
    };
}

} // namespace sunrise::server::activity::mission::sdk_bridge
