#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

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

/** @return The Lua-facing name of one world collection kind. */
[[nodiscard]] const char* collection_name(WorldCollectionKind kind) noexcept {
    switch (kind) {
    case WorldCollectionKind::squadAnchors:
        return "squad-anchor";
    case WorldCollectionKind::bubbles:
        return "bubble";
    case WorldCollectionKind::states:
        return "state";
    case WorldCollectionKind::objects:
        return "object";
    case WorldCollectionKind::slots:
        return "slot";
    case WorldCollectionKind::descriptors:
        return "descriptor";
    case WorldCollectionKind::embeddedPlacementLinks:
        return "embedded-placement-link";
    case WorldCollectionKind::authoredPlacements:
        return "authored-placement";
    case WorldCollectionKind::embeddedPlacements:
        return "embedded-placement";
    case WorldCollectionKind::typedReferences:
        return "typed-reference";
    case WorldCollectionKind::containerPlacementLists:
        return "container-placement-list";
    case WorldCollectionKind::containerPlacementOwners:
        return "container-placement-owner";
    case WorldCollectionKind::containerPlacements:
        return "container-placement";
    case WorldCollectionKind::containerPlacementConfigs:
        return "container-placement-config";
    case WorldCollectionKind::containerPlacementComponents:
        return "container-placement-component";
    case WorldCollectionKind::type23PlacementLinks:
        return "type23-placement-link";
    case WorldCollectionKind::type23PlacementCandidates:
        return "type23-placement-candidate";
    case WorldCollectionKind::staticSpatialTables:
        return "static-spatial-table";
    case WorldCollectionKind::staticSpatialOwners:
        return "static-spatial-owner";
    case WorldCollectionKind::staticSpatialInstances:
        return "static-spatial-instance";
    case WorldCollectionKind::triggerVolumeTables:
        return "trigger-volume-table";
    case WorldCollectionKind::triggerVolumeOwners:
        return "trigger-volume-owner";
    case WorldCollectionKind::triggerVolumeIncomingReferences:
        return "trigger-volume-incoming-reference";
    case WorldCollectionKind::triggerVolumes:
        return "trigger-volume";
    case WorldCollectionKind::triggerVolumeVertices:
        return "trigger-volume-vertex";
    case WorldCollectionKind::triggerVolumeTriangles:
        return "trigger-volume-triangle";
    case WorldCollectionKind::names:
        return "name";
    case WorldCollectionKind::tagNames:
        return "tag-name";
    case WorldCollectionKind::nameCandidates:
        return "name-candidate";
    case WorldCollectionKind::inlineNameCandidates:
        return "inline-name-candidate";
    case WorldCollectionKind::authoredSquadConfigContexts:
        return "authored-squad-config-context";
    case WorldCollectionKind::authoredSquadPlacementContexts:
        return "authored-squad-placement-context";
    case WorldCollectionKind::authoredSquadPointContexts:
        return "authored-squad-point-context";
    case WorldCollectionKind::authoredSquadPointPlacementMatches:
        return "authored-squad-point-placement-match";
    case WorldCollectionKind::authoredSquadEdgeContexts:
        return "authored-squad-edge-context";
    }
    return "unknown";
}

/** Maps a Lua collection name to its kind. @return False when the name is unknown. */
[[nodiscard]] bool collection_kind(std::string_view key, WorldCollectionKind& output) noexcept {
    struct Entry final {
        std::string_view key;
        WorldCollectionKind kind;
    };
    // Every generated-world collection a program may index, by its script-facing key.
    static constexpr std::array entries{
        Entry{"bubbles", WorldCollectionKind::bubbles},
        Entry{"states", WorldCollectionKind::states},
        Entry{"objects", WorldCollectionKind::objects},
        Entry{"slots", WorldCollectionKind::slots},
        Entry{"descriptors", WorldCollectionKind::descriptors},
        Entry{"embedded_placement_links", WorldCollectionKind::embeddedPlacementLinks},
        Entry{"embedded_placements", WorldCollectionKind::embeddedPlacements},
        Entry{"typed_references", WorldCollectionKind::typedReferences},
        Entry{"authored_placements", WorldCollectionKind::authoredPlacements},
        Entry{"container_placement_lists", WorldCollectionKind::containerPlacementLists},
        Entry{"container_placement_owners", WorldCollectionKind::containerPlacementOwners},
        Entry{"container_placements", WorldCollectionKind::containerPlacements},
        Entry{"container_placement_configs", WorldCollectionKind::containerPlacementConfigs},
        Entry{"container_placement_components", WorldCollectionKind::containerPlacementComponents},
        Entry{"type23_placement_links", WorldCollectionKind::type23PlacementLinks},
        Entry{"type23_placement_candidates", WorldCollectionKind::type23PlacementCandidates},
        Entry{"static_spatial_tables", WorldCollectionKind::staticSpatialTables},
        Entry{"static_spatial_owners", WorldCollectionKind::staticSpatialOwners},
        Entry{"static_spatial_instances", WorldCollectionKind::staticSpatialInstances},
        Entry{"trigger_volume_tables", WorldCollectionKind::triggerVolumeTables},
        Entry{"trigger_volume_owners", WorldCollectionKind::triggerVolumeOwners},
        Entry{"trigger_volume_incoming_references",
              WorldCollectionKind::triggerVolumeIncomingReferences},
        Entry{"trigger_volumes", WorldCollectionKind::triggerVolumes},
        Entry{"trigger_volume_vertices", WorldCollectionKind::triggerVolumeVertices},
        Entry{"trigger_volume_triangles", WorldCollectionKind::triggerVolumeTriangles},
        Entry{"names", WorldCollectionKind::names},
        Entry{"tag_names", WorldCollectionKind::tagNames},
        Entry{"name_candidates", WorldCollectionKind::nameCandidates},
        Entry{"inline_name_candidates", WorldCollectionKind::inlineNameCandidates},
        Entry{"authored_squad_config_contexts", WorldCollectionKind::authoredSquadConfigContexts},
        Entry{"authored_squad_placement_contexts",
              WorldCollectionKind::authoredSquadPlacementContexts},
        Entry{"authored_squad_point_contexts", WorldCollectionKind::authoredSquadPointContexts},
        Entry{"authored_squad_point_placement_matches",
              WorldCollectionKind::authoredSquadPointPlacementMatches},
        Entry{"authored_squad_edge_contexts", WorldCollectionKind::authoredSquadEdgeContexts},
    };
    const auto found = std::find_if(
        entries.begin(), entries.end(), [key](const Entry& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    output = found->kind;
    return true;
}

/** @return The Lua-facing name of one catalog name-provenance value. */
[[nodiscard]] const char* provenance_name(std::uint32_t value) noexcept {
    switch (static_cast<catalog::NameProvenance>(value)) {
    case catalog::NameProvenance::unresolved:
        return "unresolved";
    case catalog::NameProvenance::packageInline:
        return "package_inline";
    case catalog::NameProvenance::packagePath:
        return "package_path";
    case catalog::NameProvenance::packageIdentifierCandidate:
        return "package_identifier_candidate";
    }
    return nullptr;
}

/** Pushes one 32-byte digest as 64 lowercase hex characters. */
void push_digest(lua_State* state, const std::array<std::byte, 32>& digest) {
    // Lowercase hex digits; every digest and byte field is spelled this way.
    constexpr char digits[] = "0123456789abcdef";
    std::array<char, 64> text{};
    for (std::size_t index = 0; index < digest.size(); ++index) {
        const auto value = static_cast<std::uint8_t>(digest[index]);
        text[index * 2] = digits[value >> 4U];
        text[index * 2 + 1] = digits[value & 0xFU];
    }
    lua_pushlstring(state, text.data(), text.size());
}

void push_u64(lua_State* state, std::uint64_t value) {
    std::array<char, 32> text{};
    const auto result = std::to_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{}) {
        luaL_error(state, "world identity conversion failed");
    }
    lua_pushlstring(state, text.data(), static_cast<std::size_t>(result.ptr - text.data()));
}

void push_world(lua_State* state, const WorldDefinitionApi& api) {
    push_handle(state, kWorldMetatable, WorldHandle{api.generation});
}

void push_collection(lua_State* state,
                     const WorldGenerationIdentity& generation,
                     WorldCollectionKind kind) {
    push_handle(state, kWorldCollectionMetatable, WorldCollectionHandle{generation, kind});
}

void push_squad_anchor_collection(lua_State* state,
                                  const WorldGenerationIdentity& generation,
                                  std::uint32_t squadRow) {
    push_handle(
        state, kSquadAnchorCollectionMetatable, SquadAnchorCollectionHandle{generation, squadRow});
}

void push_row(lua_State* state,
              const WorldGenerationIdentity& generation,
              WorldCollectionKind kind,
              std::uint32_t localRow) {
    push_handle(state, kWorldRowMetatable, WorldRowHandle{generation, kind, localRow});
}

/** Lua `at` for a world collection: resolves one 1-based row. Errors on a stale generation. */
[[nodiscard]] int world_collection_at(lua_State* state) {
    const auto* const collection = static_cast<const WorldCollectionHandle*>(
        luaL_checkudata(state, 1, kWorldCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, collection->generation)) {
        return luaL_error(state, "generated-world collection is stale");
    }
    const lua_Integer selected = luaL_checkinteger(state, 2);
    WorldRowDefinition row{};
    if (selected <= 0
        || static_cast<std::uint64_t>(selected) > (std::numeric_limits<std::uint32_t>::max)()
        || !owner->definitions.world.resolve(owner->definitions.world.context,
                                             collection->generation,
                                             collection->kind,
                                             static_cast<std::uint32_t>(selected),
                                             row)) {
        return luaL_error(state, "generated-world row is unavailable");
    }
    push_row(state, collection->generation, collection->kind, static_cast<std::uint32_t>(selected));
    return 1;
}

/** Lua index for a world collection: `count` and `at`, else nil. */
[[nodiscard]] int world_collection_index(lua_State* state) {
    const auto* const collection = static_cast<const WorldCollectionHandle*>(
        luaL_checkudata(state, 1, kWorldCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, collection->generation)) {
        return luaL_error(state, "generated-world collection is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "count") {
        lua_pushinteger(
            state,
            static_cast<lua_Integer>(owner->definitions.world.count(
                owner->definitions.world.context, collection->generation, collection->kind)));
    } else if (key == "at") {
        lua_pushcfunction(state, &world_collection_at);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua `at` for a squad-anchor collection: resolves one 1-based anchor of that squad. */
[[nodiscard]] int squad_anchor_collection_at(lua_State* state) {
    const auto* const collection = static_cast<const SquadAnchorCollectionHandle*>(
        luaL_checkudata(state, 1, kSquadAnchorCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, collection->generation)) {
        return luaL_error(state, "squad-anchor collection is stale");
    }
    const lua_Integer selected = luaL_checkinteger(state, 2);
    SquadAnchorDefinition anchor{};
    if (selected <= 0
        || static_cast<std::uint64_t>(selected) > (std::numeric_limits<std::uint32_t>::max)()
        || !owner->definitions.world.resolveSquadAnchor(owner->definitions.world.context,
                                                        collection->generation,
                                                        collection->squadRow,
                                                        static_cast<std::uint32_t>(selected),
                                                        anchor)) {
        return luaL_error(state, "squad-anchor row is unavailable");
    }
    push_row(
        state, collection->generation, WorldCollectionKind::squadAnchors, anchor.collectionRow);
    return 1;
}

/** Lua index for a squad-anchor collection: `count` and `at`, else nil. */
[[nodiscard]] int squad_anchor_collection_index(lua_State* state) {
    const auto* const collection = static_cast<const SquadAnchorCollectionHandle*>(
        luaL_checkudata(state, 1, kSquadAnchorCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, collection->generation)) {
        return luaL_error(state, "squad-anchor collection is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "count") {
        lua_pushinteger(
            state,
            static_cast<lua_Integer>(owner->definitions.world.squadAnchorCount(
                owner->definitions.world.context, collection->generation, collection->squadRow)));
    } else if (key == "at") {
        lua_pushcfunction(state, &squad_anchor_collection_at);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua `at` for a trigger geometry collection: resolves one 1-based vertex or triangle. */
[[nodiscard]] int geometry_collection_at(lua_State* state) {
    const auto* const collection = static_cast<const GeometryCollectionHandle*>(
        luaL_checkudata(state, 1, kGeometryCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, collection->generation)) {
        return luaL_error(state, "trigger geometry collection is stale");
    }
    const lua_Integer selected = luaL_checkinteger(state, 2);
    if (selected <= 0
        || static_cast<std::uint64_t>(selected) > (std::numeric_limits<std::uint32_t>::max)()) {
        return luaL_error(state, "trigger geometry row is unavailable");
    }
    const std::uint32_t localRow = static_cast<std::uint32_t>(selected);
    bool resolved = false;
    if (collection->kind == GeometryKind::vertices) {
        TriggerVertexDefinition row{};
        resolved = owner->definitions.world.resolveTriggerVertex(owner->definitions.world.context,
                                                                 collection->generation,
                                                                 collection->triggerRow,
                                                                 localRow,
                                                                 row);
    } else {
        TriggerTriangleDefinition row{};
        resolved = owner->definitions.world.resolveTriggerTriangle(owner->definitions.world.context,
                                                                   collection->generation,
                                                                   collection->triggerRow,
                                                                   localRow,
                                                                   row);
    }
    if (!resolved) {
        return luaL_error(state, "trigger geometry row is unavailable");
    }
    push_handle(state,
                collection->kind == GeometryKind::vertices ? kTriggerVertexMetatable
                                                           : kTriggerTriangleMetatable,
                TriggerGeometryHandle{
                    collection->generation, collection->kind, collection->triggerRow, localRow});
    return 1;
}

/** Lua index for a trigger geometry collection: `count` and `at`, else nil. */
[[nodiscard]] int geometry_collection_index(lua_State* state) {
    const auto* const collection = static_cast<const GeometryCollectionHandle*>(
        luaL_checkudata(state, 1, kGeometryCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, collection->generation)) {
        return luaL_error(state, "trigger geometry collection is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "count") {
        WorldRowDefinition row{};
        if (!owner->definitions.world.resolve(owner->definitions.world.context,
                                              collection->generation,
                                              WorldCollectionKind::triggerVolumes,
                                              collection->triggerRow,
                                              row)) {
            return luaL_error(state, "trigger volume is stale");
        }
        lua_pushinteger(state,
                        collection->kind == GeometryKind::vertices
                            ? row.triggerVolume.vertexCount
                            : row.triggerVolume.triangleCount);
    } else if (key == "at") {
        lua_pushcfunction(state, &geometry_collection_at);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for the generated-world view: scenario identity, collections and diagnostics. */
[[nodiscard]] int world_index(lua_State* state) {
    const auto* const handle =
        static_cast<const WorldHandle*>(luaL_checkudata(state, 1, kWorldMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "generated-world view is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "scenario_tag") {
        lua_pushinteger(state, handle->generation.scenarioTag);
    } else if (key == "scenario_name") {
        lua_pushlstring(state,
                        owner->definitions.world.scenarioName.data(),
                        owner->definitions.world.scenarioName.size());
    } else if (key == "sdk_build_sha256") {
        push_digest(state, handle->generation.sdkBuildSha256);
    } else if (key == "sdk_payload_sha256") {
        push_digest(state, handle->generation.sdkPayloadSha256);
    } else if (key == "source_fingerprint") {
        push_digest(state, handle->generation.sourceFingerprint);
    } else if (key == "manifest_payload_sha256") {
        push_digest(state, handle->generation.manifestPayloadSha256);
    } else if (key == "shard_payload_sha256") {
        push_digest(state, handle->generation.shardPayloadSha256);
    } else if (WorldCollectionKind kind{}; collection_kind(key, kind)) {
        push_collection(state, handle->generation, kind);
    } else if (key == "diagnostics") {
        push_handle(state, kWorldDiagnosticsMetatable, WorldDiagnosticsHandle{handle->generation});
    } else if (key == "coverage") {
        push_coverage_collection(state, handle->generation);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

} // namespace

bool current(const Impl& owner, const WorldGenerationIdentity& generation) noexcept {
    const WorldDefinitionApi& api = owner.definitions.world;
    return api.context != nullptr && api.validate != nullptr && api.count != nullptr
           && api.resolve != nullptr && api.resolveField != nullptr
           && api.squadAnchorCount != nullptr && api.resolveSquadAnchor != nullptr
           && api.resolveTriggerVertex != nullptr && api.resolveTriggerTriangle != nullptr
           && api.resolveDiagnostics != nullptr && api.coverageCount != nullptr
           && api.resolveCoverage != nullptr && api.generation == generation
           && api.validate(api.context, generation);
}

void push_vector(lua_State* state, std::span<const float> value) {
    if (value.empty() || value.size() > 4) {
        luaL_error(state, "world vector lane count is invalid");
    }
    VectorHandle handle{};
    std::copy(value.begin(), value.end(), handle.value.begin());
    handle.lanes = static_cast<std::uint8_t>(value.size());
    push_handle(state, kVectorMetatable, handle);
}

/** Pushes the stable `world/<scenario>/<kind>/<row>` text id for one row. */
void push_world_id(lua_State* state,
                   const WorldGenerationIdentity& generation,
                   WorldCollectionKind kind,
                   std::uint32_t row) {
    std::array<char, 96> text{};
    char* cursor = text.data();
    char* const end = text.data() + text.size();
    // Every generated-world text id starts with this family prefix.
    constexpr std::string_view prefix = "world/";
    std::copy(prefix.begin(), prefix.end(), cursor);
    cursor += prefix.size();
    const auto scenario = std::to_chars(cursor, end, generation.scenarioTag, 16);
    if (scenario.ec != std::errc{}) {
        luaL_error(state, "world id scenario conversion failed");
    }
    cursor = scenario.ptr;
    *cursor++ = '/';
    const std::string_view family = collection_name(kind);
    std::copy(family.begin(), family.end(), cursor);
    cursor += family.size();
    *cursor++ = '/';
    const auto converted = std::to_chars(cursor, end, row);
    if (converted.ec != std::errc{}) {
        luaL_error(state, "world id row conversion failed");
    }
    lua_pushlstring(state, text.data(), static_cast<std::size_t>(converted.ptr - text.data()));
}

void push_geometry_collection(lua_State* state,
                              const WorldGenerationIdentity& generation,
                              GeometryKind kind,
                              std::uint32_t triggerRow) {
    push_handle(state,
                kGeometryCollectionMetatable,
                GeometryCollectionHandle{generation, kind, triggerRow});
}

void push_optional_row(lua_State* state, std::uint32_t value) {
    if (value == 0) {
        lua_pushnil(state);
    } else {
        lua_pushinteger(state, value);
    }
}

void push_optional_name(lua_State* state, std::string_view value) {
    if (value.empty()) {
        lua_pushnil(state);
    } else {
        lua_pushlstring(state, value.data(), value.size());
    }
}

/** Pushes the lane of one world name field the key selects: value, provenance, or count. */
void push_name_field(lua_State* state, std::string_view key, const WorldNameDefinition& value) {
    if (key.ends_with("_name")) {
        push_optional_name(state, value.value);
    } else if (key.ends_with("_name_provenance")) {
        push_optional_name(state, provenance_name(value.provenance));
    } else if (key.ends_with("_name_provenance_code")) {
        lua_pushinteger(state, value.provenance);
    } else if (key.ends_with("_name_candidate_count")) {
        lua_pushinteger(state, value.candidateCount);
    } else if (key.ends_with("_name_source_tag")) {
        lua_pushinteger(state, value.sourceTag);
    } else if (key.ends_with("_name_source_class")) {
        lua_pushinteger(state, value.sourceClassId);
    } else if (key.ends_with("_name_strongest_tier_overflow")) {
        lua_pushboolean(state, value.strongestTierOverflow ? 1 : 0);
    } else {
        lua_pushnil(state);
    }
}

bool resolved_row(lua_State* state, const WorldRowHandle& handle, WorldRowDefinition& output) {
    Impl* const owner = impl_from_state(state);
    return owner != nullptr && current(*owner, handle.generation)
           && owner->definitions.world.resolve(owner->definitions.world.context,
                                               handle.generation,
                                               handle.kind,
                                               handle.localRow,
                                               output);
}

/** Resolves one field of a world row. @return False when the generation is stale. */
bool resolved_field(lua_State* state,
                    const WorldRowHandle& handle,
                    std::string_view key,
                    WorldFieldDefinition& output) {
    Impl* const owner = impl_from_state(state);
    return owner != nullptr && current(*owner, handle.generation)
           && owner->definitions.world.resolveField(owner->definitions.world.context,
                                                    handle.generation,
                                                    handle.kind,
                                                    handle.localRow,
                                                    key,
                                                    output);
}

/** Pushes one resolved world field, mapping its kind to the matching Lua value. */
void push_field(lua_State* state, const WorldFieldDefinition& value) {
    switch (value.kind) {
    case WorldFieldKind::absent:
        lua_pushnil(state);
        return;
    case WorldFieldKind::unsignedInteger:
        lua_pushinteger(state, static_cast<lua_Integer>(value.unsignedValue));
        return;
    case WorldFieldKind::signedInteger:
        lua_pushinteger(state, static_cast<lua_Integer>(value.signedValue));
        return;
    case WorldFieldKind::unsignedDecimalString:
        push_u64(state, value.unsignedValue);
        return;
    case WorldFieldKind::signedDecimalString: {
        std::array<char, 32> text{};
        const auto result =
            std::to_chars(text.data(), text.data() + text.size(), value.signedValue);
        if (result.ec != std::errc{}) {
            luaL_error(state, "world signed value conversion failed");
        }
        lua_pushlstring(state, text.data(), static_cast<std::size_t>(result.ptr - text.data()));
        return;
    }
    case WorldFieldKind::real:
        lua_pushnumber(state, value.realValue);
        return;
    case WorldFieldKind::boolean:
        lua_pushboolean(state, value.unsignedValue != 0 ? 1 : 0);
        return;
    case WorldFieldKind::string:
        lua_pushlstring(state, value.stringValue.data(), value.stringValue.size());
        return;
    case WorldFieldKind::optionalString:
        push_optional_name(state, value.stringValue);
        return;
    case WorldFieldKind::optionalRow:
        push_optional_row(state, static_cast<std::uint32_t>(value.unsignedValue));
        return;
    case WorldFieldKind::vector:
        push_vector(state, std::span(value.vectorValue.data(), value.valueCount));
        return;
    case WorldFieldKind::bytes: {
        // Lowercase hex digits; every digest and byte field is spelled this way.
        constexpr char digits[] = "0123456789abcdef";
        std::array<char, 64> text{};
        if (value.valueCount > value.bytesValue.size()) {
            luaL_error(state, "world byte field exceeds its declared bound");
        }
        for (std::size_t index = 0; index < value.valueCount; ++index) {
            const auto byte = static_cast<std::uint8_t>(value.bytesValue[index]);
            text[index * 2U] = digits[byte >> 4U];
            text[index * 2U + 1U] = digits[byte & 0xFU];
        }
        lua_pushlstring(state, text.data(), static_cast<std::size_t>(value.valueCount) * 2U);
        return;
    }
    }
    lua_pushnil(state);
}

/** Lua index for a vector handle: `x`, `y`, `z` and `w` select a lane, any other key is nil. */
int vector_index(lua_State* state) {
    const auto* const handle =
        static_cast<const VectorHandle*>(luaL_checkudata(state, 1, kVectorMetatable));
    const std::string_view key = checked_key(state);
    std::size_t lane = handle->lanes;
    if (key == "x") {
        lane = 0;
    } else if (key == "y") {
        lane = 1;
    } else if (key == "z") {
        lane = 2;
    } else if (key == "w") {
        lane = 3;
    } else if (key == "count") {
        lua_pushinteger(state, handle->lanes);
        return 1;
    }
    if (lane >= handle->lanes) {
        lua_pushnil(state);
    } else {
        lua_pushnumber(state, handle->value[lane]);
    }
    return 1;
}

/** Registers every world-API metatable against its index function. */
void register_metatables(lua_State* state) {
    register_metatable(state, kWorldMetatable, &world_index);
    register_metatable(state, kWorldCollectionMetatable, &world_collection_index);
    register_metatable(state, kSquadAnchorCollectionMetatable, &squad_anchor_collection_index);
    register_metatable(state, kWorldRowMetatable, &world_row_index);
    register_metatable(state, kGeometryCollectionMetatable, &geometry_collection_index);
    register_metatable(state, kTriggerVertexMetatable, &trigger_vertex_index);
    register_metatable(state, kTriggerTriangleMetatable, &trigger_triangle_index);
    register_metatable(state, kVectorMetatable, &vector_index);
    register_metatable(state, kWorldDiagnosticsMetatable, &world_diagnostics_index);
    register_metatable(state, kWorldCoverageCollectionMetatable, &world_coverage_collection_index);
    register_metatable(state, kWorldCoverageMetatable, &world_coverage_index);
}

/** Serves the activity table's `world` and `squad_anchors` members. @return False for others. */
bool push_activity_member(lua_State* state, std::string_view key) {
    if (key != "world" && key != "squad_anchors") {
        return false;
    }
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, owner->definitions.world.generation)) {
        lua_pushnil(state);
        return true;
    }
    if (key == "world") {
        push_world(state, owner->definitions.world);
    } else {
        push_collection(
            state, owner->definitions.world.generation, WorldCollectionKind::squadAnchors);
    }
    return true;
}

/** Serves the squad table's `anchors` member. @return False for any other key. */
bool push_squad_member(lua_State* state, const SquadDefinition& squad, std::string_view key) {
    if (key != "anchors") {
        return false;
    }
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, owner->definitions.world.generation)) {
        lua_pushnil(state);
        return true;
    }
    push_squad_anchor_collection(state, owner->definitions.world.generation, squad.localRow);
    return true;
}

} // namespace sunrise::server::activity::mission::lua_vm::detail::world_api
