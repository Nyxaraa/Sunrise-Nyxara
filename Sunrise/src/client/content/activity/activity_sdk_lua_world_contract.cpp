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

/** Builds one projection row: its surface, view, artifact, sections and coverage. */
[[nodiscard]] Value projection(std::string_view surface,
                               std::string_view viewName,
                               std::string_view artifact,
                               std::initializer_list<std::string_view> sections,
                               std::initializer_list<std::string_view> coverageFamilies,
                               std::string_view coverageSource) {
    Value::Array sourceSections;
    Value::Array families;
    sourceSections.reserve(sections.size());
    families.reserve(coverageFamilies.size());
    for (const std::string_view section : sections) {
        sourceSections.push_back(string(section));
    }
    for (const std::string_view family : coverageFamilies) {
        families.push_back(string(family));
    }
    return object({
        {"coverage_families", array(std::move(families))},
        {"coverage_source", string(coverageSource)},
        {"source_artifact", string(artifact)},
        {"source_sections", array(std::move(sourceSections))},
        {"surface", string(surface)},
        {"view", string(viewName)},
    });
}

/** Builds the per-family projection ledger the world contract publishes. */
[[nodiscard]] Value projection_ledger() {
    return array({
        projection("context.sdk.squad_anchors",
                   "SquadAnchorView",
                   "activity_sdk.runtime_pack",
                   {"squads", "squad_anchors"},
                   {},
                   "activity_sdk.full_catalog"),
        projection("context.sdk.squads[].anchors",
                   "SquadAnchorView",
                   "activity_sdk.runtime_pack",
                   {"squads", "squad_anchors"},
                   {},
                   "activity_sdk.full_catalog"),
        projection("context.sdk.world.authored_placements",
                   "WorldAuthoredPlacementView",
                   "generated_world.scenario_shard",
                   {"authored_placements", "tag_names", "name_candidates"},
                   {"authored_placements", "names"},
                   "context.sdk.world.coverage"),
        projection(
            "context.sdk.world.container_placements",
            "WorldContainerPlacementView",
            "generated_world.scenario_shard",
            {"container_placement_lists", "container_placements", "tag_names", "name_candidates"},
            {"container_placements", "names"},
            "context.sdk.world.coverage"),
        projection("context.sdk.world.coverage",
                   "WorldCoverageView",
                   "generated_world.scenario_shard",
                   {"scalars.coverage_diagnostics"},
                   {"scenario_topology",
                    "object_graph",
                    "typed_references",
                    "authored_placements",
                    "container_placements",
                    "embedded_placements",
                    "type23_placements",
                    "static_spatial",
                    "trigger_volumes",
                    "names"},
                   "self"),
        projection("context.sdk.world.diagnostics",
                   "WorldDiagnosticsView",
                   "generated_world.scenario_shard",
                   {"scalars"},
                   {"scenario_topology",
                    "object_graph",
                    "typed_references",
                    "authored_placements",
                    "container_placements",
                    "embedded_placements",
                    "type23_placements",
                    "static_spatial",
                    "trigger_volumes",
                    "names"},
                   "context.sdk.world.coverage"),
        projection(
            "context.sdk.world.embedded_placements",
            "WorldEmbeddedPlacementView",
            "generated_world.scenario_shard",
            {"embedded_placement_links", "embedded_placements", "tag_names", "name_candidates"},
            {"embedded_placements", "names"},
            "context.sdk.world.coverage"),
        projection(
            "context.sdk.world.static_spatial_instances",
            "WorldStaticSpatialView",
            "generated_world.scenario_shard",
            {"static_spatial_tables", "static_spatial_instances", "tag_names", "name_candidates"},
            {"static_spatial", "names"},
            "context.sdk.world.coverage"),
        projection(
            "context.sdk.world.trigger_volumes",
            "WorldTriggerVolumeView",
            "generated_world.scenario_shard",
            {"trigger_volume_tables", "trigger_volume_instances", "tag_names", "name_candidates"},
            {"trigger_volumes", "names"},
            "context.sdk.world.coverage"),
        projection("context.sdk.world.trigger_volumes[].triangles",
                   "WorldTriggerTriangleView",
                   "generated_world.scenario_shard",
                   {"trigger_volume_instances", "trigger_volume_triangles"},
                   {"trigger_volumes"},
                   "context.sdk.world.coverage"),
        projection("context.sdk.world.trigger_volumes[].vertices",
                   "WorldTriggerVertexView",
                   "generated_world.scenario_shard",
                   {"trigger_volume_instances", "trigger_volume_vertices"},
                   {"trigger_volumes"},
                   "context.sdk.world.coverage"),
    });
}

/** Builds one section-scoped projection row. */
[[nodiscard]] Value section_projection(std::uint32_t index,
                                       std::string_view section,
                                       std::string_view surface,
                                       std::string_view coverageFamily,
                                       std::string_view projectionMode = "row_collection") {
    return object({
        {"coverage_family", string(coverageFamily)},
        {"projection_mode", string(projectionMode)},
        {"section", string(section)},
        {"section_index", number(index)},
        {"source_artifact", string("generated_world.scenario_shard")},
        {"surface", string(surface)},
    });
}

/** One deterministic row for every authenticated section in generated_world::SectionIndex. */
[[nodiscard]] Value section_coverage_ledger() {
    return array({
        section_projection(0, "bubbles", "context.sdk.world.bubbles", "scenario_topology"),
        section_projection(1, "states", "context.sdk.world.states", "scenario_topology"),
        section_projection(2, "objects", "context.sdk.world.objects", "object_graph"),
        section_projection(3, "slots", "context.sdk.world.slots", "object_graph"),
        section_projection(4, "descriptors", "context.sdk.world.descriptors", "object_graph"),
        section_projection(5,
                           "embedded_placement_links",
                           "context.sdk.world.embedded_placement_links",
                           "embedded_placements"),
        section_projection(6,
                           "embedded_placements",
                           "context.sdk.world.embedded_placements",
                           "embedded_placements"),
        section_projection(
            7, "references", "context.sdk.world.typed_references", "typed_references"),
        section_projection(8,
                           "authored_placements",
                           "context.sdk.world.authored_placements",
                           "authored_placements"),
        section_projection(9,
                           "container_placement_lists",
                           "context.sdk.world.container_placement_lists",
                           "container_placements"),
        section_projection(10,
                           "container_placement_owners",
                           "context.sdk.world.container_placement_owners",
                           "container_placements"),
        section_projection(11,
                           "container_placements",
                           "context.sdk.world.container_placements",
                           "container_placements"),
        section_projection(12,
                           "container_placement_configs",
                           "context.sdk.world.container_placement_configs",
                           "container_placements"),
        section_projection(13,
                           "container_placement_components",
                           "context.sdk.world.container_placement_components",
                           "container_placements"),
        section_projection(14,
                           "type23_placement_links",
                           "context.sdk.world.type23_placement_links",
                           "type23_placements"),
        section_projection(15,
                           "type23_placement_candidates",
                           "context.sdk.world.type23_placement_candidates",
                           "type23_placements"),
        section_projection(16,
                           "static_spatial_tables",
                           "context.sdk.world.static_spatial_tables",
                           "static_spatial"),
        section_projection(17,
                           "static_spatial_owners",
                           "context.sdk.world.static_spatial_owners",
                           "static_spatial"),
        section_projection(18,
                           "static_spatial_instances",
                           "context.sdk.world.static_spatial_instances",
                           "static_spatial"),
        section_projection(19,
                           "trigger_volume_tables",
                           "context.sdk.world.trigger_volume_tables",
                           "trigger_volumes"),
        section_projection(20,
                           "trigger_volume_owners",
                           "context.sdk.world.trigger_volume_owners",
                           "trigger_volumes"),
        section_projection(21,
                           "trigger_volume_incoming_references",
                           "context.sdk.world.trigger_volume_incoming_references",
                           "trigger_volumes"),
        section_projection(
            22, "trigger_volume_instances", "context.sdk.world.trigger_volumes", "trigger_volumes"),
        section_projection(23,
                           "trigger_volume_vertices",
                           "context.sdk.world.trigger_volume_vertices",
                           "trigger_volumes"),
        section_projection(24,
                           "trigger_volume_triangles",
                           "context.sdk.world.trigger_volume_triangles",
                           "trigger_volumes"),
        section_projection(25, "names", "context.sdk.world.names", "names"),
        section_projection(26, "tag_names", "context.sdk.world.tag_names", "names"),
        section_projection(27, "name_candidates", "context.sdk.world.name_candidates", "names"),
        section_projection(
            28, "inline_name_candidates", "context.sdk.world.inline_name_candidates", "names"),
        section_projection(29,
                           "inline_name_bytes",
                           "context.sdk.world.inline_name_candidates[].value",
                           "names",
                           "authenticated_candidate_bounded_string"),
        // The squad-graph sections carry their own completeness flag instead of a BuildCoverage
        // family, so their family name does not appear in the coverage collection.
        section_projection(30,
                           "authored_squad_config_contexts",
                           "context.sdk.world.authored_squad_config_contexts",
                           "authored_squad_graph",
                           "row_collection_self_gated"),
        section_projection(31,
                           "authored_squad_placement_contexts",
                           "context.sdk.world.authored_squad_placement_contexts",
                           "authored_squad_graph",
                           "row_collection_self_gated"),
        section_projection(32,
                           "authored_squad_point_contexts",
                           "context.sdk.world.authored_squad_point_contexts",
                           "authored_squad_graph",
                           "row_collection_self_gated"),
        section_projection(33,
                           "authored_squad_point_placement_matches",
                           "context.sdk.world.authored_squad_point_placement_matches",
                           "authored_squad_graph",
                           "row_collection_self_gated"),
        section_projection(34,
                           "authored_squad_edge_contexts",
                           "context.sdk.world.authored_squad_edge_contexts",
                           "authored_squad_graph",
                           "row_collection_self_gated"),
    });
}

[[nodiscard]] Value views() {
    Value::Object output;
    append_world_core_views(output);
    append_world_spatial_views(output);
    sort_keys(output);
    return object(std::move(output));
}
} // namespace

/** Assembles the whole world SDK contract value. @return False when a build step throws. */
bool build_world_sdk_contract_value(Value& output) noexcept {
    try {
        output = object({
            {"collection_contracts",
             object({
                 {"owned_rows",
                  object({{"fields", field_map({{"count", "u32"}})},
                          {"index_base", number(1)},
                          {"methods",
                           object({{"at",
                                    object({{"arguments", string_array({"row"})},
                                            {"errors", string("throws_unavailable_or_stale")},
                                            {"returns", string("declared_view")}})}})}})},
                 {"rows",
                  object({{"fields", field_map({{"count", "u32"}})},
                          {"index_base", number(1)},
                          {"methods",
                           object({{"at",
                                    object({{"arguments", string_array({"row"})},
                                            {"errors", string("throws_unavailable_or_stale")},
                                            {"returns", string("declared_view")}})}})}})},
             })},
            {"coverage_contract",
             object({
                 {"accepted_full_statuses",
                  string_array({"complete", "not_applicable", "preserved_unresolved"})},
                 {"families",
                  string_array({"scenario_topology",
                                "object_graph",
                                "typed_references",
                                "authored_placements",
                                "container_placements",
                                "embedded_placements",
                                "type23_placements",
                                "static_spatial",
                                "trigger_volumes",
                                "names"})},
                 {"loss_bits",
                  object({{"dropped", number(2)}, {"partial", number(4)}, {"unread", number(1)}})},
                 {"path", string("context.sdk.world.coverage")},
                 {"source", string("generated_world.scalars.coverage_diagnostics")},
                 {"status_codes",
                  object({{"complete", number(1)},
                          {"incomplete", number(4)},
                          {"not_applicable", number(2)},
                          {"preserved_unresolved", number(3)},
                          {"unassessed", number(0)}})},
             })},
            {"exact_generation",
             object({
                 {"handle_fields",
                  string_array({"sdk_build_sha256",
                                "sdk_payload_sha256",
                                "source_fingerprint",
                                "manifest_payload_sha256",
                                "shard_payload_sha256",
                                "activity_client_generation",
                                "activity_row",
                                "scenario_tag"})},
                 {"read_policy", string("revalidate_all_fields_before_every_borrowed_row_read")},
                 {"stale_policy", string("throw_without_rebinding")},
             })},
            {"projection_ledger", projection_ledger()},
            {"projection_scope", string("all_retained_generated_world_sections")},
            {"section_coverage_ledger", section_coverage_ledger()},
            {"root_extensions",
             object({
                 {"ActivityView",
                  object({{"squad_anchors", string("WorldRowCollection<SquadAnchorView>")},
                          {"world", string("WorldView")}})},
                 {"SquadView",
                  object({{"anchors", string("OwnedRowCollection<SquadAnchorView>")}})},
             })},
            {"schema", string("sunrise-generated-world-lua-sdk-v1")},
            {"views", views()},
        });
        return true;
    } catch (...) {
        output = {};
        return false;
    }
}

} // namespace sunrise::client::content::activity::sdk_generation::lua_artifacts::internal
