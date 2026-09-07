#include <utility>

#include "activity_sdk_lua_artifacts_internal.h"

namespace sunrise::client::content::activity::sdk_generation::lua_artifacts::internal {
namespace {

[[nodiscard]] Value view(Value::Object fields) {
    return object({
        {"fields", object(std::move(fields))},
        {"immutable", boolean(true)},
        {"stale_read", string("throws")},
    });
}

/** Builds the declared field shape of every manifest view. */
[[nodiscard]] Value manifest_views() {
    return object({
        {"ManifestActivityRootView",
         view({
             {"activity_root_tag", string("u32")},
             {"preferred_name", string("bounded_utf8_copy")},
             {"row", string("u32_1_based_manifest")},
             {"scenario_tag", string("u32")},
             {"selection_status", string("u8_enum_code")},
             {"selection_status_name", string("stable_enum_name")},
             {"transition_descriptor_tag", string("u32")},
         })},
        {"ManifestActivityVariantView",
         view({
             {"activity_index", string("u32")},
             {"activity_root_candidate_tags", string("ManifestOwnedTagCollection")},
             {"activity_root_candidate_tags_count", string("u32")},
             {"activity_root_candidate_tags_first_index", string("u32_zero_based_flattened")},
             {"activity_root_tag", string("u32_or_absent_tag")},
             {"binding_disposition", string("u8_enum_code")},
             {"binding_disposition_name", string("stable_enum_name")},
             {"binding_evidence_basis", string("u8_enum_code")},
             {"binding_evidence_basis_name", string("stable_enum_name")},
             {"binding_locators", string("ManifestOwnedRowCollection<ManifestPackageLocatorView>")},
             {"binding_locators_count", string("u32")},
             {"binding_locators_first_index", string("u32_zero_based_flattened")},
             {"binding_reason", string("u8_enum_code")},
             {"binding_reason_name", string("stable_enum_name")},
             {"definition_hash", string("u32")},
             {"evidence_root_tags", string("ManifestOwnedTagCollection")},
             {"evidence_root_tags_count", string("u32")},
             {"evidence_root_tags_first_index", string("u32_zero_based_flattened")},
             {"full_sdk_acceptable", string("bool")},
             {"has_internal_name", string("bool")},
             {"has_matchmaking_config", string("bool")},
             {"internal_name", string("bounded_utf8_copy")},
             {"join_status", string("u8_enum_code")},
             {"join_status_name", string("stable_enum_name")},
             {"matchmaking_config_tag", string("u32_or_absent_tag")},
             {"row", string("u32_1_based_manifest")},
             {"runnable_status", string("u8_enum_code")},
             {"runnable_status_name", string("stable_enum_name")},
             {"scenario_name_candidate_tags", string("ManifestOwnedTagCollection")},
             {"scenario_name_candidate_tags_count", string("u32")},
             {"scenario_name_candidate_tags_first_index", string("u32_zero_based_flattened")},
             {"scenario_tag", string("u32_or_absent_tag")},
         })},
        {"ManifestBindingCompletenessView",
         view({
             {"fixed_scenario", string("u32")},
             {"named_definition_unavailable", string("u32")},
             {"no_direct_fixed_activity_name", string("u32")},
             {"row", string("u32_1_based_manifest")},
             {"status", string("u8_enum_code")},
             {"status_name", string("stable_enum_name")},
             {"total", string("u32")},
             {"unresolved_runnable", string("u32")},
         })},
        {"ManifestPackageLocatorView",
         view({
             {"offset", string("u64_decimal_string")},
             {"row", string("u32_1_based_owned_range")},
             {"tag", string("u32")},
         })},
        {"ManifestScenarioView",
         view({
             {"row", string("u32_1_based_manifest")},
             {"scenario_name", string("bounded_utf8_copy")},
             {"scenario_tag", string("u32")},
             {"shard_payload_sha256", string("hex_sha256")},
         })},
        {"ManifestView",
         view({
             {"activity_client_generation", string("u64_decimal_string")},
             {"activity_roots", string("ManifestRowCollection<ManifestActivityRootView>")},
             {"activity_row", string("u32_1_based_runtime_pack")},
             {"activity_variants", string("ManifestRowCollection<ManifestActivityVariantView>")},
             {"binding_completeness", string("ManifestBindingCompletenessView")},
             {"format_version", string("u32_constant_4")},
             {"manifest_payload_sha256", string("hex_sha256")},
             {"scenario_tag", string("u32")},
             {"scenarios", string("ManifestRowCollection<ManifestScenarioView>")},
             {"sdk_build_sha256", string("hex_sha256")},
             {"sdk_payload_sha256", string("hex_sha256")},
             {"shard_payload_sha256", string("hex_sha256")},
             {"source_fingerprint", string("hex_sha256")},
         })},
    });
}

/** Builds the per-family projection ledger the manifest contract publishes. */
[[nodiscard]] Value projection_ledger() {
    return array({
        object({
            {"family", string("manifest_header")},
            {"family_index", number(0)},
            {"projection_mode", string("copied_authenticated_header")},
            {"source_artifact", string("generated_world.catalog.bin.v4")},
            {"surface",
             string("context.sdk.manifest.<format_version,source_fingerprint,sdk_build_sha256,"
                    "sdk_payload_sha256,manifest_payload_sha256>")},
        }),
        object({
            {"family", string("bound_generation_identity")},
            {"family_index", number(1)},
            {"projection_mode", string("copied_exact_world_binding_identity")},
            {"source_artifact", string("generated_world.runtime.binding")},
            {"surface",
             string("context.sdk.manifest.<shard_payload_sha256,activity_client_generation,"
                    "activity_row,scenario_tag>")},
        }),
        object({
            {"family", string("binding_completeness")},
            {"family_index", number(2)},
            {"projection_mode", string("singleton_view")},
            {"source_artifact", string("generated_world.catalog.bin.v4")},
            {"surface", string("context.sdk.manifest.binding_completeness")},
            {"view", string("ManifestBindingCompletenessView")},
        }),
        object({
            {"family", string("scenario_records")},
            {"family_index", number(3)},
            {"projection_mode", string("row_collection")},
            {"source_artifact", string("generated_world.catalog.bin.v4")},
            {"surface", string("context.sdk.manifest.scenarios")},
            {"view", string("ManifestScenarioView")},
        }),
        object({
            {"family", string("activity_root_records")},
            {"family_index", number(4)},
            {"projection_mode", string("row_collection")},
            {"source_artifact", string("generated_world.catalog.bin.v4")},
            {"surface", string("context.sdk.manifest.activity_roots")},
            {"view", string("ManifestActivityRootView")},
        }),
        object({
            {"family", string("activity_variant_records")},
            {"family_index", number(5)},
            {"projection_mode", string("row_collection")},
            {"source_artifact", string("generated_world.catalog.bin.v4")},
            {"surface", string("context.sdk.manifest.activity_variants")},
            {"view", string("ManifestActivityVariantView")},
        }),
        object({
            {"family", string("activity_variant_evidence_pools")},
            {"family_index", number(6)},
            {"projection_mode", string("authenticated_variant_owned_ranges")},
            {"source_artifact", string("generated_world.catalog.bin.v4")},
            {"surface",
             string("context.sdk.manifest.activity_variants[].{activity_root_candidate_tags,"
                    "scenario_name_candidate_tags,evidence_root_tags,binding_locators}")},
            {"view", string("ManifestPackageLocatorView")},
        }),
    });
}

/** Builds the declared shape of every manifest collection. */
[[nodiscard]] Value collection_contracts() {
    const auto collection = [](std::string_view order, std::string_view returns) {
        return object({
            {"canonical_order", string(order)},
            {"fields", object({{"count", string("u32")}})},
            {"index_base", number(1)},
            {"methods",
             object({{"at",
                      object({{"arguments", string_array({"row"})},
                              {"errors", string("throws_out_of_range_or_stale")},
                              {"returns", string(returns)}})}})},
        });
    };
    return object({
        {"ManifestOwnedRowCollection",
         collection("authenticated_package_locator_pool_order", "ManifestPackageLocatorView")},
        {"ManifestOwnedTagCollection",
         collection("authenticated_shared_evidence_tag_pool_order", "u32")},
        {"ManifestRowCollection",
         collection("authenticated_manifest_record_order", "declared_view")},
    });
}

} // namespace

/** Assembles the whole manifest SDK contract value. @return False when a build step throws. */
bool build_manifest_sdk_contract_value(Value& output) noexcept {
    try {
        output = object({
            {"availability", string("post_exact_world_bind")},
            {"before_scenario_bind_audit",
             string("native_resolver_authenticates_manifest_identity_and_scenario_record")},
            {"collection_contracts", collection_contracts()},
            {"direct_collection_count", number(3)},
            {"family_count", number(7)},
            {"generation_validation",
             object({
                 {"borrowed_read_rule",
                  string("revalidate_all_identity_fields_before_every_borrowed_read")},
                 {"fields",
                  string_array({"sdk_build_sha256",
                                "sdk_payload_sha256",
                                "source_fingerprint",
                                "manifest_payload_sha256",
                                "shard_payload_sha256",
                                "activity_client_generation",
                                "activity_row",
                                "scenario_tag"})},
                 {"stale_read", string("throws")},
             })},
            {"lua_root", string("context.sdk.manifest")},
            {"projection_ledger", projection_ledger()},
            {"projection_scope", string("all_retained_generated_world_manifest_v4_families")},
            {"schema", string("sunrise-generated-world-manifest-lua-sdk-v1")},
            {"variant_runtime_pack_parity",
             object({
                 {"runtime_pack_surface", string("context.sdk.catalog.activities")},
                 {"status", string("lossless_scalar_and_owned_evidence_mirror")},
             })},
            {"views", manifest_views()},
        });
        return true;
    } catch (...) {
        output = {};
        return false;
    }
}

} // namespace sunrise::client::content::activity::sdk_generation::lua_artifacts::internal
