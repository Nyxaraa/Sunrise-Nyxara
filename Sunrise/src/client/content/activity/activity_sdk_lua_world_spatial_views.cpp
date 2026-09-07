#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "activity_sdk_lua_artifacts_internal.h"

namespace sunrise::client::content::activity::sdk_generation::lua_artifacts::internal {
namespace {

using Field = std::pair<std::string_view, std::string_view>;

void sort_keys(Value::Object& value) {
    std::sort(value.begin(), value.end(), [](const auto& first, const auto& second) {
        return first.first < second.first;
    });
}

[[nodiscard]] Value field_map(std::initializer_list<Field> source) {
    Value::Object output;
    output.reserve(source.size());
    for (const auto& [name, type] : source) {
        output.push_back({std::string(name), string(type)});
    }
    sort_keys(output);
    return object(std::move(output));
}

/** Appends the seven declared name lanes for one field prefix. */
void append_name_fields(Value::Object& output, std::string_view prefix) {
    const auto add = [&output, prefix](std::string_view suffix, std::string_view type) {
        std::string name(prefix);
        name.append(suffix);
        output.push_back({std::move(name), string(type)});
    };
    add("_name", "string_or_nil");
    add("_name_provenance", "unresolved_package_inline_package_path_or_identifier_candidate");
    add("_name_provenance_code", "u32");
    add("_name_candidate_count", "u32");
    add("_name_source_tag", "u32");
    add("_name_source_class", "u32");
    add("_name_strongest_tier_overflow", "boolean");
}

/** Builds one view's declared fields, expanding each prefix into its name lanes. */
[[nodiscard]] Value named_view(std::initializer_list<Field> source,
                               std::initializer_list<std::string_view> prefixes,
                               std::string_view rowScope) {
    Value::Object fields;
    fields.reserve(source.size() + prefixes.size() * 7U);
    for (const auto& [name, type] : source) {
        fields.push_back({std::string(name), string(type)});
    }
    for (const std::string_view prefix : prefixes) {
        append_name_fields(fields, prefix);
    }
    sort_keys(fields);
    return object({
        {"fields", object(std::move(fields))},
        {"immutable", boolean(true)},
        {"row_scope", string(rowScope)},
        {"stale_read", string("throws")},
    });
}

[[nodiscard]] Value view(std::initializer_list<Field> fields, std::string_view rowScope) {
    return object({
        {"fields", field_map(fields)},
        {"immutable", boolean(true)},
        {"row_scope", string(rowScope)},
        {"stale_read", string("throws")},
    });
}

} // namespace

/** Appends the declared shape of every spatial world view. */
void append_world_spatial_views(Value::Object& output) {
    output.push_back({"WorldStaticSpatialView",
                      named_view({{"id", "string"},
                                  {"row", "u32_1_based_shard_section"},
                                  {"table_row", "u32_1_based"},
                                  {"instance_index", "u32_0_based"},
                                  {"table_tag", "u32"},
                                  {"bounds_tag", "u32"},
                                  {"resource_tag", "u32"},
                                  {"resource_name_row", "u32_1_based_or_nil"},
                                  {"table_complete", "boolean"},
                                  {"rotation", "WorldVectorView_4"},
                                  {"position", "WorldVectorView_4"},
                                  {"scale", "WorldVectorView_4"},
                                  {"local_minimum", "WorldVectorView_4"},
                                  {"local_maximum", "WorldVectorView_4"},
                                  {"bounds_opaque", "hex_16_bytes"}},
                                 {"table", "bounds", "resource"},
                                 "generated_world.static_spatial_instances")});
    output.push_back({"WorldStaticSpatialTableView",
                      named_view({{"id", "string"},
                                  {"row", "u32_1_based_shard_section"},
                                  {"table_tag", "u32"},
                                  {"bounds_tag", "u32"},
                                  {"first_instance_row", "u32_1_based_or_nil"},
                                  {"instance_count", "u32"},
                                  {"table_name_row", "u32_1_based_or_nil"},
                                  {"bounds_name_row", "u32_1_based_or_nil"},
                                  {"complete", "boolean"}},
                                 {"table", "bounds"},
                                 "generated_world.static_spatial_tables")});
    output.push_back({"WorldStaticSpatialOwnerView",
                      named_view({{"id", "string"},
                                  {"row", "u32_1_based_shard_section"},
                                  {"table_row", "u32_1_based_or_nil"},
                                  {"placement_row", "u32_1_based_or_nil"},
                                  {"container_tag", "u32"},
                                  {"object_list_tag", "u32"},
                                  {"parent_tag", "u32"},
                                  {"object_list_entry", "u32_0_based"},
                                  {"container_name_row", "u32_1_based_or_nil"},
                                  {"object_list_name_row", "u32_1_based_or_nil"},
                                  {"parent_name_row", "u32_1_based_or_nil"},
                                  {"scenario_bubble_mask", "u64_decimal_string"},
                                  {"map_bubble_mask", "hex_32_bytes"},
                                  {"context", "spatial_context_or_nil"},
                                  {"context_code", "u32"}},
                                 {"container", "object_list", "parent"},
                                 "generated_world.static_spatial_owners")});
    output.push_back({"WorldTriggerVolumeView",
                      named_view({{"id", "string"},
                                  {"row", "u32_1_based_shard_section"},
                                  {"table_row", "u32_1_based"},
                                  {"authored_row_index", "u32_0_based"},
                                  {"config_tag", "u32"},
                                  {"identity_match_count", "u32"},
                                  {"registry_key", "u32"},
                                  {"component_ordinal", "u32_0_based"},
                                  {"slot_index", "u32"},
                                  {"slot_type", "u32"},
                                  {"class_definition_tag", "u32"},
                                  {"class_definition_name_row", "u32_1_based_or_nil"},
                                  {"shape_resource_tag", "u32"},
                                  {"shape_resource_name_row", "u32_1_based_or_nil"},
                                  {"shape_reference_word", "u32"},
                                  {"shape_index", "u32_0_based"},
                                  {"first_vertex_row", "u32_1_based_or_nil"},
                                  {"vertex_count", "u32"},
                                  {"first_triangle_row", "u32_1_based_or_nil"},
                                  {"triangle_count", "u32"},
                                  {"flags", "u32"},
                                  {"extrusion", "real32"},
                                  {"active", "u8"},
                                  {"table_complete", "boolean"},
                                  {"complete", "boolean"},
                                  {"rotation", "WorldVectorView_4"},
                                  {"position", "WorldVectorView_4"},
                                  {"minimum", "WorldVectorView_4"},
                                  {"maximum", "WorldVectorView_4"},
                                  {"vertices", "WorldTriggerVertexCollection"},
                                  {"triangles", "WorldTriggerTriangleCollection"}},
                                 {"config", "class_definition", "shape_resource"},
                                 "generated_world.trigger_volume_instances")});
    output.push_back({"WorldTriggerVolumeTableView",
                      named_view({{"id", "string"},
                                  {"row", "u32_1_based_shard_section"},
                                  {"config_tag", "u32"},
                                  {"config_name_row", "u32_1_based_or_nil"},
                                  {"first_instance_row", "u32_1_based_or_nil"},
                                  {"instance_count", "u32"},
                                  {"identity_match_count", "u32"},
                                  {"registry_key", "u32"},
                                  {"component_ordinal", "u32_0_based"},
                                  {"slot_index", "u16"},
                                  {"slot_type", "u8"},
                                  {"complete", "boolean"}},
                                 {"config"},
                                 "generated_world.trigger_volume_tables")});
    output.push_back({"WorldTriggerVolumeOwnerView",
                      view({{"id", "string"},
                            {"row", "u32_1_based_shard_section"},
                            {"table_row", "u32_1_based_or_nil"},
                            {"object_row", "u32_1_based_or_nil"},
                            {"slot_row", "u32_1_based_or_nil"},
                            {"slot_match_count", "u32"},
                            {"first_incoming_reference_row", "u32_1_based_or_nil"},
                            {"incoming_reference_count", "u32"},
                            {"incoming_reference_match_count", "u32"},
                            {"slot_join", "reference_join_or_nil"},
                            {"slot_join_code", "u32"}},
                           "generated_world.trigger_volume_owners")});
    output.push_back({"WorldTriggerVolumeIncomingReferenceView",
                      view({{"id", "string"},
                            {"row", "u32_1_based_shard_section"},
                            {"owner_row", "u32_1_based_or_nil"},
                            {"reference_row", "u32_1_based_or_nil"},
                            {"source_object_row", "u32_1_based_or_nil"},
                            {"source_slot_row", "u32_1_based_or_nil"}},
                           "generated_world.trigger_volume_incoming_references")});
    output.push_back({"WorldGlobalTriggerVertexView",
                      view({{"id", "string"},
                            {"row", "u32_1_based_shard_section"},
                            {"value", "WorldVectorView_4"}},
                           "generated_world.trigger_volume_vertices")});
    output.push_back({"WorldGlobalTriggerTriangleView",
                      view({{"id", "string"},
                            {"row", "u32_1_based_shard_section"},
                            {"a", "u8_vertex_index"},
                            {"b", "u8_vertex_index"},
                            {"c", "u8_vertex_index"}},
                           "generated_world.trigger_volume_triangles")});
    output.push_back({"WorldTriggerVertexView",
                      view({{"id", "string"},
                            {"row", "u32_1_based_shard_section"},
                            {"local_row", "u32_1_based_volume"},
                            {"value", "WorldVectorView_4"}},
                           "generated_world.trigger_volume_vertices")});
    output.push_back({"WorldTriggerTriangleView",
                      view({{"id", "string"},
                            {"row", "u32_1_based_shard_section"},
                            {"local_row", "u32_1_based_volume"},
                            {"a", "u8_vertex_index"},
                            {"b", "u8_vertex_index"},
                            {"c", "u8_vertex_index"}},
                           "generated_world.trigger_volume_triangles")});
    output.push_back(
        {"WorldNameView",
         named_view(
             {{"id", "string"},
              {"row", "u32_1_based_shard_section"},
              {"hash", "u32"},
              {"first_candidate_row", "u32_1_based_or_nil"},
              {"candidate_count", "u32"},
              {"selected_candidate_row", "u32_1_based_or_nil"},
              {"provenance", "unresolved_package_inline_package_path_or_identifier_candidate"},
              {"provenance_code", "u32"},
              {"strongest_tier_overflow", "boolean"}},
             {"selected"},
             "generated_world.names")});
    output.push_back(
        {"WorldTagNameView",
         named_view(
             {{"id", "string"},
              {"row", "u32_1_based_shard_section"},
              {"tag", "u32"},
              {"class_id", "u32"},
              {"first_candidate_row", "u32_1_based_or_nil"},
              {"candidate_count", "u32"},
              {"selected_candidate_row", "u32_1_based_or_nil"},
              {"provenance", "unresolved_package_inline_package_path_or_identifier_candidate"},
              {"provenance_code", "u32"}},
             {"selected"},
             "generated_world.tag_names")});
    output.push_back(
        {"WorldNameCandidateView",
         view({{"id", "string"},
               {"row", "u32_1_based_shard_section"},
               {"value", "string"},
               {"source_tag", "u32"},
               {"source_class_id", "u32"},
               {"length", "u16"},
               {"provenance", "unresolved_package_inline_package_path_or_identifier_candidate"},
               {"provenance_code", "u32"}},
              "generated_world.name_candidates")});
    output.push_back({"WorldInlineNameCandidateView",
                      view({{"id", "string"},
                            {"row", "u32_1_based_shard_section"},
                            {"hash", "u32"},
                            {"first_byte", "u32_0_based"},
                            {"byte_count", "u32"},
                            {"value", "authenticated_bounded_utf8_string"}},
                           "generated_world.inline_name_candidates_and_inline_name_bytes")});
    output.push_back({"WorldCoverageView",
                      view({{"row", "u32_1_based"},
                            {"family", "string"},
                            {"family_index", "u32_0_based"},
                            {"status", "string"},
                            {"status_code", "u32"},
                            {"loss_mask", "u8"},
                            {"unread", "boolean"},
                            {"dropped", "boolean"},
                            {"partial", "boolean"}},
                           "generated_world.scalars.coverage_diagnostics")});
    output.push_back({"WorldDiagnosticsView",
                      view({{"detail", "string"},
                            {"revision", "u64_decimal_string"},
                            {"request", "u64_decimal_string"},
                            {"status", "string_or_nil"},
                            {"status_code", "u32"},
                            {"coverage", "string_or_nil"},
                            {"coverage_code", "u32"},
                            {"unresolved_reads", "u64_decimal_string"},
                            {"container_unresolved_reads", "u64_decimal_string"},
                            {"container_semantic_unresolved", "u64_decimal_string"},
                            {"container_dropped_lists", "u64_decimal_string"},
                            {"container_dropped_owners", "u64_decimal_string"},
                            {"container_dropped_placements", "u64_decimal_string"},
                            {"container_dropped_configs", "u64_decimal_string"},
                            {"container_dropped_components", "u64_decimal_string"},
                            {"container_dropped_behaviors", "u64_decimal_string"},
                            {"behavior_referenced_behaviors", "u64_decimal_string"},
                            {"behavior_unique_roots", "u64_decimal_string"},
                            {"behavior_read_programs", "u64_decimal_string"},
                            {"behavior_unread_programs", "u64_decimal_string"},
                            {"behavior_structurally_partial_programs", "u64_decimal_string"},
                            {"behavior_output_partial_programs", "u64_decimal_string"},
                            {"behavior_dropped_programs", "u64_decimal_string"},
                            {"behavior_dropped_texts", "u64_decimal_string"},
                            {"embedded_applicable_descriptors", "u64_decimal_string"},
                            {"embedded_empty_descriptors", "u64_decimal_string"},
                            {"embedded_read_placements", "u64_decimal_string"},
                            {"embedded_unread_configurations", "u64_decimal_string"},
                            {"embedded_malformed_descriptors", "u64_decimal_string"},
                            {"embedded_malformed_placements", "u64_decimal_string"},
                            {"embedded_unresolved_class_definitions", "u64_decimal_string"},
                            {"embedded_dropped_links", "u64_decimal_string"},
                            {"embedded_dropped_placements", "u64_decimal_string"},
                            {"type23_unread_identifiers", "u64_decimal_string"},
                            {"type23_dropped_links", "u64_decimal_string"},
                            {"type23_dropped_candidates", "u64_decimal_string"},
                            {"type23_zero_identity_matches", "u64_decimal_string"},
                            {"type23_multiple_identity_matches", "u64_decimal_string"},
                            {"type23_zero_active_candidates", "u64_decimal_string"},
                            {"type23_multiple_active_candidates", "u64_decimal_string"},
                            {"static_spatial_unresolved_reads", "u64_decimal_string"},
                            {"static_spatial_semantic_unresolved", "u64_decimal_string"},
                            {"static_spatial_dropped", "u64_decimal_string"},
                            {"trigger_unresolved_reads", "u64_decimal_string"},
                            {"trigger_dropped_tables", "u64_decimal_string"},
                            {"trigger_dropped_owners", "u64_decimal_string"},
                            {"trigger_dropped_instances", "u64_decimal_string"},
                            {"trigger_dropped_vertices", "u64_decimal_string"},
                            {"trigger_dropped_triangles", "u64_decimal_string"},
                            {"trigger_dropped_incoming_references", "u64_decimal_string"},
                            {"trigger_zero_matches", "u64_decimal_string"},
                            {"trigger_multiple_matches", "u64_decimal_string"},
                            {"container_context_resolved", "boolean"},
                            {"container_context_not_applicable", "boolean"},
                            {"container_behavior_inventory_resolved", "boolean"},
                            {"container_identity_owner_inventory_complete", "boolean"},
                            {"container_complete", "boolean"},
                            {"behavior_inventory_resolved", "boolean"},
                            {"behavior_complete", "boolean"},
                            {"embedded_complete", "boolean"},
                            {"type23_complete", "boolean"},
                            {"static_spatial_context_resolved", "boolean"},
                            {"static_spatial_not_applicable", "boolean"},
                            {"static_spatial_complete", "boolean"},
                            {"trigger_complete", "boolean"}},
                           "generated_world.scalars")});
}

} // namespace sunrise::client::content::activity::sdk_generation::lua_artifacts::internal
