#include <array>
#include <charconv>
#include <limits>

#include "../../../state/build_data/scriptables/coverage.h"
#include "../../../state/build_data/scriptables/definition.h"
#include "mission_script_lua_world_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail::world_api {
namespace {

namespace catalog = state::build_data::scriptables;

[[nodiscard]] std::string_view checked_key(lua_State* state) {
    std::size_t length = 0;
    const char* const value = luaL_checklstring(state, 2, &length);
    return {value, length};
}

/** @return The Lua-facing name of one world build status. */
[[nodiscard]] const char* build_status_name(std::uint32_t value) noexcept {
    switch (static_cast<catalog::BuildStatus>(value)) {
    case catalog::BuildStatus::idle:
        return "idle";
    case catalog::BuildStatus::queued:
        return "queued";
    case catalog::BuildStatus::building:
        return "building";
    case catalog::BuildStatus::ready:
        return "ready";
    case catalog::BuildStatus::failed:
        return "failed";
    }
    return nullptr;
}

[[nodiscard]] const char* coverage_name(std::uint32_t value) noexcept {
    switch (static_cast<catalog::BuildCoverage>(value)) {
    case catalog::BuildCoverage::none:
        return "none";
    case catalog::BuildCoverage::full:
        return "full";
    }
    return nullptr;
}

/** @return The Lua-facing name of one structural family coverage status. */
[[nodiscard]] const char* family_coverage_status_name(std::uint32_t value) noexcept {
    switch (static_cast<catalog::FamilyCoverageStatus>(value)) {
    case catalog::FamilyCoverageStatus::unassessed:
        return "unassessed";
    case catalog::FamilyCoverageStatus::complete:
        return "complete";
    case catalog::FamilyCoverageStatus::notApplicable:
        return "not_applicable";
    case catalog::FamilyCoverageStatus::preservedUnresolved:
        return "preserved_unresolved";
    case catalog::FamilyCoverageStatus::incomplete:
        return "incomplete";
    }
    return nullptr;
}

void push_u64(lua_State* state, std::uint64_t value) {
    std::array<char, 32> text{};
    const auto result = std::to_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{}) {
        luaL_error(state, "world identity conversion failed");
    }
    lua_pushlstring(state, text.data(), static_cast<std::size_t>(result.ptr - text.data()));
}

} // namespace

void push_coverage_collection(lua_State* state, const WorldGenerationIdentity& generation) {
    push_handle(
        state, kWorldCoverageCollectionMetatable, WorldCoverageCollectionHandle{generation});
}

/** Lua `at` for the coverage collection: resolves one 1-based structural family. */
[[nodiscard]] int world_coverage_collection_at(lua_State* state) {
    const auto* const collection = static_cast<const WorldCoverageCollectionHandle*>(
        luaL_checkudata(state, 1, kWorldCoverageCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    const lua_Integer selected = luaL_checkinteger(state, 2);
    WorldCoverageDefinition row{};
    if (owner == nullptr || !current(*owner, collection->generation)) {
        return luaL_error(state, "generated-world coverage is stale");
    }
    if (selected <= 0
        || static_cast<std::uint64_t>(selected) > (std::numeric_limits<std::uint32_t>::max)()
        || !owner->definitions.world.resolveCoverage(owner->definitions.world.context,
                                                     collection->generation,
                                                     static_cast<std::uint32_t>(selected),
                                                     row)) {
        return luaL_error(state, "generated-world coverage row is unavailable");
    }
    push_handle(state,
                kWorldCoverageMetatable,
                WorldCoverageHandle{collection->generation, static_cast<std::uint32_t>(selected)});
    return 1;
}

/** Lua index for the coverage collection: `count` and `at`, else nil. */
[[nodiscard]] int world_coverage_collection_index(lua_State* state) {
    const auto* const collection = static_cast<const WorldCoverageCollectionHandle*>(
        luaL_checkudata(state, 1, kWorldCoverageCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, collection->generation)) {
        return luaL_error(state, "generated-world coverage is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "count") {
        lua_pushinteger(state,
                        static_cast<lua_Integer>(owner->definitions.world.coverageCount(
                            owner->definitions.world.context, collection->generation)));
    } else if (key == "at") {
        lua_pushcfunction(state, &world_coverage_collection_at);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for one coverage row: its family, status and counts. */
[[nodiscard]] int world_coverage_index(lua_State* state) {
    const auto* const handle =
        static_cast<const WorldCoverageHandle*>(luaL_checkudata(state, 1, kWorldCoverageMetatable));
    Impl* const owner = impl_from_state(state);
    WorldCoverageDefinition row{};
    if (owner == nullptr || !current(*owner, handle->generation)
        || !owner->definitions.world.resolveCoverage(
            owner->definitions.world.context, handle->generation, handle->localRow, row)) {
        return luaL_error(state, "generated-world coverage row is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "row") {
        lua_pushinteger(state, row.row);
    } else if (key == "family") {
        lua_pushlstring(state, row.family.data(), row.family.size());
    } else if (key == "family_index") {
        lua_pushinteger(state, row.familyIndex);
    } else if (key == "status") {
        const char* const status = family_coverage_status_name(row.status);
        push_optional_name(state, status == nullptr ? std::string_view{} : status);
    } else if (key == "status_code") {
        lua_pushinteger(state, row.status);
    } else if (key == "loss_mask") {
        lua_pushinteger(state, row.lossMask);
    } else if (key == "unread") {
        lua_pushboolean(state, (row.lossMask & catalog::kFamilyCoverageLossUnread) != 0 ? 1 : 0);
    } else if (key == "dropped") {
        lua_pushboolean(state, (row.lossMask & catalog::kFamilyCoverageLossDropped) != 0 ? 1 : 0);
    } else if (key == "partial") {
        lua_pushboolean(state, (row.lossMask & catalog::kFamilyCoverageLossPartial) != 0 ? 1 : 0);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for the world diagnostics view: build status, counts and the coverage set. */
[[nodiscard]] int world_diagnostics_index(lua_State* state) {
    const auto* const handle = static_cast<const WorldDiagnosticsHandle*>(
        luaL_checkudata(state, 1, kWorldDiagnosticsMetatable));
    Impl* const owner = impl_from_state(state);
    WorldDiagnosticsDefinition row{};
    if (owner == nullptr || !current(*owner, handle->generation)
        || !owner->definitions.world.resolveDiagnostics(
            owner->definitions.world.context, handle->generation, row)) {
        return luaL_error(state, "generated-world diagnostics are stale");
    }
    const std::string_view key = checked_key(state);
#define WORLD_DIAGNOSTIC_U64(lua_key, member)                                                      \
    if (key == (lua_key)) {                                                                        \
        push_u64(state, row.member);                                                               \
    } else
#define WORLD_DIAGNOSTIC_BOOL(lua_key, member)                                                     \
    if (key == (lua_key)) {                                                                        \
        lua_pushboolean(state, row.member ? 1 : 0);                                                \
    } else
    WORLD_DIAGNOSTIC_U64("revision", revision)
    WORLD_DIAGNOSTIC_U64("request", request)
    WORLD_DIAGNOSTIC_U64("unresolved_reads", unresolvedReads)
    WORLD_DIAGNOSTIC_U64("container_unresolved_reads", containerUnresolvedReads)
    WORLD_DIAGNOSTIC_U64("container_semantic_unresolved", containerSemanticUnresolved)
    WORLD_DIAGNOSTIC_U64("container_dropped_lists", containerDroppedLists)
    WORLD_DIAGNOSTIC_U64("container_dropped_owners", containerDroppedOwners)
    WORLD_DIAGNOSTIC_U64("container_dropped_placements", containerDroppedPlacements)
    WORLD_DIAGNOSTIC_U64("container_dropped_configs", containerDroppedConfigs)
    WORLD_DIAGNOSTIC_U64("container_dropped_components", containerDroppedComponents)
    WORLD_DIAGNOSTIC_U64("embedded_applicable_descriptors", embeddedApplicableDescriptors)
    WORLD_DIAGNOSTIC_U64("embedded_empty_descriptors", embeddedEmptyDescriptors)
    WORLD_DIAGNOSTIC_U64("embedded_read_placements", embeddedReadPlacements)
    WORLD_DIAGNOSTIC_U64("embedded_unread_configurations", embeddedUnreadConfigurations)
    WORLD_DIAGNOSTIC_U64("embedded_malformed_descriptors", embeddedMalformedDescriptors)
    WORLD_DIAGNOSTIC_U64("embedded_malformed_placements", embeddedMalformedPlacements)
    WORLD_DIAGNOSTIC_U64("embedded_unresolved_class_definitions",
                         embeddedUnresolvedClassDefinitions)
    WORLD_DIAGNOSTIC_U64("embedded_dropped_links", embeddedDroppedLinks)
    WORLD_DIAGNOSTIC_U64("embedded_dropped_placements", embeddedDroppedPlacements)
    WORLD_DIAGNOSTIC_U64("type23_unread_identifiers", type23UnreadIdentifiers)
    WORLD_DIAGNOSTIC_U64("type23_dropped_links", type23DroppedLinks)
    WORLD_DIAGNOSTIC_U64("type23_dropped_candidates", type23DroppedCandidates)
    WORLD_DIAGNOSTIC_U64("type23_zero_identity_matches", type23ZeroIdentityMatches)
    WORLD_DIAGNOSTIC_U64("type23_multiple_identity_matches", type23MultipleIdentityMatches)
    WORLD_DIAGNOSTIC_U64("type23_zero_active_candidates", type23ZeroActiveCandidates)
    WORLD_DIAGNOSTIC_U64("type23_multiple_active_candidates", type23MultipleActiveCandidates)
    WORLD_DIAGNOSTIC_U64("static_spatial_unresolved_reads", staticSpatialUnresolvedReads)
    WORLD_DIAGNOSTIC_U64("static_spatial_semantic_unresolved", staticSpatialSemanticUnresolved)
    WORLD_DIAGNOSTIC_U64("static_spatial_dropped", staticSpatialDropped)
    WORLD_DIAGNOSTIC_U64("trigger_unresolved_reads", triggerUnresolvedReads)
    WORLD_DIAGNOSTIC_U64("trigger_dropped_tables", triggerDroppedTables)
    WORLD_DIAGNOSTIC_U64("trigger_dropped_owners", triggerDroppedOwners)
    WORLD_DIAGNOSTIC_U64("trigger_dropped_instances", triggerDroppedInstances)
    WORLD_DIAGNOSTIC_U64("trigger_dropped_vertices", triggerDroppedVertices)
    WORLD_DIAGNOSTIC_U64("trigger_dropped_triangles", triggerDroppedTriangles)
    WORLD_DIAGNOSTIC_U64("trigger_dropped_incoming_references", triggerDroppedIncomingReferences)
    WORLD_DIAGNOSTIC_U64("trigger_zero_matches", triggerZeroMatches)
    WORLD_DIAGNOSTIC_U64("trigger_multiple_matches", triggerMultipleMatches)
    WORLD_DIAGNOSTIC_BOOL("container_context_resolved", containerContextResolved)
    WORLD_DIAGNOSTIC_BOOL("container_context_not_applicable", containerContextNotApplicable)
    WORLD_DIAGNOSTIC_BOOL("container_identity_owner_inventory_complete",
                          containerIdentityOwnerInventoryComplete)
    WORLD_DIAGNOSTIC_BOOL("container_complete", containerComplete)
    WORLD_DIAGNOSTIC_BOOL("embedded_complete", embeddedComplete)
    WORLD_DIAGNOSTIC_BOOL("type23_complete", type23Complete)
    WORLD_DIAGNOSTIC_BOOL("static_spatial_context_resolved", staticSpatialContextResolved)
    WORLD_DIAGNOSTIC_BOOL("static_spatial_not_applicable", staticSpatialNotApplicable)
    WORLD_DIAGNOSTIC_BOOL("static_spatial_complete", staticSpatialComplete)
    WORLD_DIAGNOSTIC_BOOL("trigger_complete", triggerComplete)
    if (key == "detail") {
        lua_pushlstring(state, row.detail.data(), row.detail.size());
    } else if (key == "status") {
        const char* const status = build_status_name(row.status);
        push_optional_name(state, status == nullptr ? std::string_view{} : status);
    } else if (key == "status_code") {
        lua_pushinteger(state, row.status);
    } else if (key == "coverage") {
        const char* const coverage = coverage_name(row.coverage);
        push_optional_name(state, coverage == nullptr ? std::string_view{} : coverage);
    } else if (key == "coverage_code") {
        lua_pushinteger(state, row.coverage);
    } else {
        lua_pushnil(state);
    }
#undef WORLD_DIAGNOSTIC_BOOL
#undef WORLD_DIAGNOSTIC_U64
    return 1;
}

} // namespace sunrise::server::activity::mission::lua_vm::detail::world_api
