#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "mission_script_lua_world_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail::world_api {
namespace {

[[nodiscard]] std::string_view checked_key(lua_State* state) {
    std::size_t length = 0;
    const char* const value = luaL_checklstring(state, 2, &length);
    return {value, length};
}

void push_u64(lua_State* state, std::uint64_t value) {
    std::array<char, 32> text{};
    const auto converted = std::to_chars(text.data(), text.data() + text.size(), value);
    if (converted.ec != std::errc{}) {
        luaL_error(state, "world identity conversion failed");
    }
    lua_pushlstring(state, text.data(), static_cast<std::size_t>(converted.ptr - text.data()));
}

void push_i64(lua_State* state, std::int64_t value) {
    std::array<char, 32> text{};
    const auto converted = std::to_chars(text.data(), text.data() + text.size(), value);
    if (converted.ec != std::errc{}) {
        luaL_error(state, "world signed identity conversion failed");
    }
    lua_pushlstring(state, text.data(), static_cast<std::size_t>(converted.ptr - text.data()));
}

/** Pushes one exact package float-bit lane as an unsigned 32-bit Lua integer. */
void push_u32(lua_State* state, std::uint32_t value) {
    lua_pushinteger(state, static_cast<lua_Integer>(value));
}

/** Pushes one byte field as lowercase hex. Errors when it exceeds its declared bound. */
void push_bytes(lua_State* state, std::span<const std::byte> value) {
    // Lowercase hex digits; every digest and byte field is spelled this way.
    constexpr char digits[] = "0123456789abcdef";
    std::array<char, 32> text{};
    if (value.size() * 2U > text.size()) {
        luaL_error(state, "world byte field exceeds its declared bound");
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto byte = static_cast<std::uint8_t>(value[index]);
        text[index * 2U] = digits[byte >> 4U];
        text[index * 2U + 1U] = digits[byte & 0xFU];
    }
    lua_pushlstring(state, text.data(), value.size() * 2U);
}

/** @return The Lua-facing name of one name-resolution context code. */
[[nodiscard]] const char* context_name(std::uint32_t value) noexcept {
    switch (value) {
    case 0:
        return "unresolved";
    case 1:
        return "package_object_state";
    case 2:
        return "package_stem_bubble";
    default:
        return nullptr;
    }
}

/** Serves the name lanes under one key prefix. @return False when the key has another prefix. */
[[nodiscard]] bool name_member(lua_State* state,
                               std::string_view key,
                               std::string_view prefix,
                               const WorldNameDefinition& name) {
    if (!key.starts_with(prefix)) {
        return false;
    }
    const std::string_view suffix = key.substr(prefix.size());
    if (suffix != "_name" && suffix != "_name_provenance" && suffix != "_name_provenance_code"
        && suffix != "_name_candidate_count" && suffix != "_name_source_tag"
        && suffix != "_name_source_class" && suffix != "_name_strongest_tier_overflow") {
        return false;
    }
    push_name_field(state, key, name);
    return true;
}

/** Pushes the stable text id for one trigger vertex or triangle. */
void push_geometry_id(lua_State* state,
                      const WorldGenerationIdentity& generation,
                      GeometryKind kind,
                      std::uint32_t row) {
    std::array<char, 96> text{};
    char* cursor = text.data();
    char* const end = text.data() + text.size();
    // Every generated-world text id starts with this family prefix.
    constexpr std::string_view prefix = "world/";
    const std::string_view family =
        kind == GeometryKind::vertices ? "trigger-vertex" : "trigger-triangle";
    cursor = std::copy(prefix.begin(), prefix.end(), cursor);
    const auto scenario = std::to_chars(cursor, end, generation.scenarioTag, 16);
    if (scenario.ec != std::errc{}) {
        luaL_error(state, "trigger geometry id scenario conversion failed");
    }
    cursor = scenario.ptr;
    *cursor++ = '/';
    cursor = std::copy(family.begin(), family.end(), cursor);
    *cursor++ = '/';
    const auto converted = std::to_chars(cursor, end, row);
    if (converted.ec != std::errc{}) {
        luaL_error(state, "trigger geometry id row conversion failed");
    }
    lua_pushlstring(state, text.data(), static_cast<std::size_t>(converted.ptr - text.data()));
}

/** Lua index for a squad-anchor row. */
[[nodiscard]] int squad_anchor_index(lua_State* state,
                                     const WorldRowHandle& handle,
                                     const SquadAnchorDefinition& row,
                                     std::string_view key) {
    if (key == "id") {
        lua_pushlstring(state, row.id.data(), row.id.size());
    } else if (key == "row") {
        lua_pushinteger(state, row.row);
    } else if (key == "collection_row") {
        lua_pushinteger(state, row.collectionRow);
    } else if (key == "squad_row") {
        lua_pushinteger(state, row.squadRow);
    } else if (key == "point_ordinal") {
        lua_pushinteger(state, row.pointOrdinal);
    } else if (key == "object_list_tag") {
        lua_pushinteger(state, row.objectListTag);
    } else if (key == "placement_ordinal") {
        lua_pushinteger(state, row.placementOrdinal);
    } else if (key == "flags") {
        lua_pushinteger(state, row.flags);
    } else if (key == "placed_entry_identity") {
        push_u64(state, row.placedEntryIdentity);
    } else if (key == "position") {
        push_vector(state, row.position);
    } else if (key == "world_id") {
        push_world_id(state, handle.generation, handle.kind, handle.localRow);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for an authored placement row. */
[[nodiscard]] int authored_index(lua_State* state,
                                 const WorldRowHandle& handle,
                                 const AuthoredPlacementDefinition& row,
                                 std::string_view key) {
    if (key == "id") {
        push_world_id(state, handle.generation, handle.kind, handle.localRow);
    } else if (key == "row") {
        lua_pushinteger(state, row.row);
    } else if (key == "source_object_row") {
        push_optional_row(state, row.sourceObjectRow);
    } else if (key == "bubble_row") {
        push_optional_row(state, row.bubbleRow);
    } else if (key == "state_row") {
        push_optional_row(state, row.stateRow);
    } else if (key == "declared_bubble_index") {
        lua_pushinteger(state, row.declaredBubbleIndex);
    } else if (key == "object_list_tag") {
        lua_pushinteger(state, row.objectListTag);
    } else if (key == "class_list_tag") {
        lua_pushinteger(state, row.classListTag);
    } else if (key == "entry_index") {
        lua_pushinteger(state, row.entryIndex);
    } else if (key == "object_list_name_row") {
        push_optional_row(state, row.objectListNameRow);
    } else if (key == "class_list_name_row") {
        push_optional_row(state, row.classListNameRow);
    } else if (key == "source_offset") {
        push_u64(state, row.sourceOffset);
    } else if (key == "rotation_bits_x") {
        push_u32(state, row.rotationBits[0]);
    } else if (key == "rotation_bits_y") {
        push_u32(state, row.rotationBits[1]);
    } else if (key == "rotation_bits_z") {
        push_u32(state, row.rotationBits[2]);
    } else if (key == "rotation_bits_w") {
        push_u32(state, row.rotationBits[3]);
    } else if (key == "position_bits_x") {
        push_u32(state, row.positionBits[0]);
    } else if (key == "position_bits_y") {
        push_u32(state, row.positionBits[1]);
    } else if (key == "position_bits_z") {
        push_u32(state, row.positionBits[2]);
    } else if (key == "uniform_scale") {
        lua_pushnumber(state, row.uniformScale);
    } else if (key == "uniform_scale_bits") {
        push_u32(state, row.uniformScaleBits);
    } else if (key == "name_hash") {
        push_u32(state, row.nameHash);
    } else if (key == "placement_flags_raw") {
        push_u32(state, row.placementFlagsRaw);
    } else if (key == "identifier") {
        push_u64(state, row.identifier);
    } else if (key == "auxiliary_relative") {
        push_i64(state, row.auxiliaryRelative);
    } else if (key == "context") {
        const char* const name = context_name(row.context);
        push_optional_name(state, name == nullptr ? std::string_view{} : name);
    } else if (key == "context_code") {
        lua_pushinteger(state, row.context);
    } else if (key == "rotation") {
        push_vector(state, row.rotation);
    } else if (key == "position") {
        push_vector(state, row.position);
    } else if (name_member(state, key, "object_list", row.objectListName)
               || name_member(state, key, "class_list", row.classListName)) {
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for an embedded placement row. */
[[nodiscard]] int embedded_index(lua_State* state,
                                 const WorldRowHandle& handle,
                                 const EmbeddedPlacementDefinition& row,
                                 std::string_view key) {
    if (key == "id") {
        push_world_id(state, handle.generation, handle.kind, handle.localRow);
    } else if (key == "row") {
        lua_pushinteger(state, row.row);
    } else if (key == "link_row") {
        push_optional_row(state, row.linkRow);
    } else if (key == "entry_index") {
        lua_pushinteger(state, row.entryIndex);
    } else if (key == "source_offset") {
        push_u64(state, row.sourceOffset);
    } else if (key == "class_list_tag") {
        lua_pushinteger(state, row.classListTag);
    } else if (key == "class_list_name_row") {
        push_optional_row(state, row.classListNameRow);
    } else if (key == "name_hash") {
        lua_pushinteger(state, row.nameHash);
    } else if (key == "identifier") {
        push_u64(state, row.identifier);
    } else if (key == "auxiliary_relative") {
        push_i64(state, row.auxiliaryRelative);
    } else if (key == "auxiliary_offset") {
        push_u64(state, row.auxiliaryOffset);
    } else if (key == "fourth_lane") {
        lua_pushnumber(state, row.fourthLane);
    } else if (key == "replication_byte") {
        lua_pushinteger(state, row.replicationByte);
    } else if (key == "gameworld_byte") {
        lua_pushinteger(state, row.gameworldByte);
    } else if (key == "object_type") {
        lua_pushinteger(state, row.objectType);
    } else if (key == "has_auxiliary") {
        lua_pushboolean(state, row.hasAuxiliary ? 1 : 0);
    } else if (key == "object_type_read") {
        lua_pushboolean(state, row.objectTypeRead ? 1 : 0);
    } else if (key == "rotation") {
        push_vector(state, row.rotation);
    } else if (key == "position") {
        push_vector(state, row.position);
    } else if (name_member(state, key, "class_list", row.classListName)) {
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for a container placement row. */
[[nodiscard]] int container_index(lua_State* state,
                                  const WorldRowHandle& handle,
                                  const ContainerPlacementDefinition& row,
                                  std::string_view key) {
    if (key == "id") {
        push_world_id(state, handle.generation, handle.kind, handle.localRow);
    } else if (key == "row") {
        lua_pushinteger(state, row.row);
    } else if (key == "list_row") {
        lua_pushinteger(state, row.listRow);
    } else if (key == "object_list_tag") {
        lua_pushinteger(state, row.objectListTag);
    } else if (key == "resource_tag") {
        lua_pushinteger(state, row.resourceTag);
    } else if (key == "resource_class") {
        lua_pushinteger(state, row.resourceClass);
    } else if (key == "entry_index") {
        lua_pushinteger(state, row.entryIndex);
    } else if (key == "class_list_tag") {
        lua_pushinteger(state, row.classListTag);
    } else if (key == "class_list_name_row") {
        push_optional_row(state, row.classListNameRow);
    } else if (key == "first_config_row") {
        push_optional_row(state, row.firstConfigRow);
    } else if (key == "config_count") {
        lua_pushinteger(state, row.configCount);
    } else if (key == "placement_identifier") {
        push_u64(state, row.placementIdentifier);
    } else if (key == "uniform_scale") {
        lua_pushnumber(state, row.uniformScale);
    } else if (key == "object_type") {
        lua_pushinteger(state, row.objectType);
    } else if (key == "resource_field_read") {
        lua_pushboolean(state, row.resourceFieldRead ? 1 : 0);
    } else if (key == "resource_resolved") {
        lua_pushboolean(state, row.resourceResolved ? 1 : 0);
    } else if (key == "list_complete") {
        lua_pushboolean(state, row.listComplete ? 1 : 0);
    } else if (key == "placement_identifier_read") {
        lua_pushboolean(state, row.placementIdentifierRead ? 1 : 0);
    } else if (key == "complete") {
        lua_pushboolean(state, row.complete ? 1 : 0);
    } else if (key == "rotation") {
        push_vector(state, row.rotation);
    } else if (key == "position") {
        push_vector(state, row.position);
    } else if (name_member(state, key, "object_list", row.objectListName)
               || name_member(state, key, "resource", row.resourceName)
               || name_member(state, key, "class_list", row.classListName)) {
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for a static spatial row. */
[[nodiscard]] int static_spatial_index(lua_State* state,
                                       const WorldRowHandle& handle,
                                       const StaticSpatialDefinition& row,
                                       std::string_view key) {
    if (key == "id") {
        push_world_id(state, handle.generation, handle.kind, handle.localRow);
    } else if (key == "row") {
        lua_pushinteger(state, row.row);
    } else if (key == "table_row") {
        lua_pushinteger(state, row.tableRow);
    } else if (key == "instance_index") {
        lua_pushinteger(state, row.instanceIndex);
    } else if (key == "table_tag") {
        lua_pushinteger(state, row.tableTag);
    } else if (key == "bounds_tag") {
        lua_pushinteger(state, row.boundsTag);
    } else if (key == "resource_tag") {
        lua_pushinteger(state, row.resourceTag);
    } else if (key == "resource_name_row") {
        push_optional_row(state, row.resourceNameRow);
    } else if (key == "table_complete") {
        lua_pushboolean(state, row.tableComplete ? 1 : 0);
    } else if (key == "rotation") {
        push_vector(state, row.rotation);
    } else if (key == "position") {
        push_vector(state, row.position);
    } else if (key == "scale") {
        push_vector(state, row.scale);
    } else if (key == "local_minimum") {
        push_vector(state, row.localMinimum);
    } else if (key == "local_maximum") {
        push_vector(state, row.localMaximum);
    } else if (key == "bounds_opaque") {
        push_bytes(state, row.boundsOpaque);
    } else if (name_member(state, key, "table", row.tableName)
               || name_member(state, key, "bounds", row.boundsName)
               || name_member(state, key, "resource", row.resourceName)) {
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for a trigger volume row, including its vertex and triangle collections. */
[[nodiscard]] int trigger_index(lua_State* state,
                                const WorldRowHandle& handle,
                                const TriggerVolumeDefinition& row,
                                std::string_view key) {
    if (key == "id") {
        push_world_id(state, handle.generation, handle.kind, handle.localRow);
    } else if (key == "row") {
        lua_pushinteger(state, row.row);
    } else if (key == "table_row") {
        lua_pushinteger(state, row.tableRow);
    } else if (key == "authored_row_index") {
        lua_pushinteger(state, row.authoredRowIndex);
    } else if (key == "config_tag") {
        lua_pushinteger(state, row.configTag);
    } else if (key == "identity_match_count") {
        lua_pushinteger(state, row.identityMatchCount);
    } else if (key == "registry_key") {
        lua_pushinteger(state, row.registryKey);
    } else if (key == "component_ordinal") {
        lua_pushinteger(state, row.componentOrdinal);
    } else if (key == "slot_index") {
        lua_pushinteger(state, row.slotIndex);
    } else if (key == "slot_type") {
        lua_pushinteger(state, row.slotType);
    } else if (key == "class_definition_tag") {
        lua_pushinteger(state, row.classDefinitionTag);
    } else if (key == "class_definition_name_row") {
        push_optional_row(state, row.classDefinitionNameRow);
    } else if (key == "shape_resource_tag") {
        lua_pushinteger(state, row.shapeResourceTag);
    } else if (key == "shape_resource_name_row") {
        push_optional_row(state, row.shapeResourceNameRow);
    } else if (key == "shape_reference_word") {
        lua_pushinteger(state, row.shapeReferenceWord);
    } else if (key == "shape_index") {
        lua_pushinteger(state, row.shapeIndex);
    } else if (key == "first_vertex_row") {
        push_optional_row(state, row.firstVertexRow);
    } else if (key == "vertex_count") {
        lua_pushinteger(state, row.vertexCount);
    } else if (key == "first_triangle_row") {
        push_optional_row(state, row.firstTriangleRow);
    } else if (key == "triangle_count") {
        lua_pushinteger(state, row.triangleCount);
    } else if (key == "flags") {
        lua_pushinteger(state, row.flags);
    } else if (key == "extrusion") {
        lua_pushnumber(state, row.extrusion);
    } else if (key == "active") {
        lua_pushinteger(state, row.active);
    } else if (key == "table_complete") {
        lua_pushboolean(state, row.tableComplete ? 1 : 0);
    } else if (key == "complete") {
        lua_pushboolean(state, row.complete ? 1 : 0);
    } else if (key == "rotation") {
        push_vector(state, row.rotation);
    } else if (key == "position") {
        push_vector(state, row.position);
    } else if (key == "minimum") {
        push_vector(state, row.minimum);
    } else if (key == "maximum") {
        push_vector(state, row.maximum);
    } else if (key == "vertices") {
        push_geometry_collection(state, handle.generation, GeometryKind::vertices, handle.localRow);
    } else if (key == "triangles") {
        push_geometry_collection(
            state, handle.generation, GeometryKind::triangles, handle.localRow);
    } else if (name_member(state, key, "config", row.configName)
               || name_member(state, key, "class_definition", row.classDefinitionName)
               || name_member(state, key, "shape_resource", row.shapeResourceName)) {
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Reads one catalog-backed world row key. Raises a Lua error when the generation is stale. */
[[nodiscard]] int
catalog_index(lua_State* state, const WorldRowHandle& handle, std::string_view key) {
    if (key == "id") {
        push_world_id(state, handle.generation, handle.kind, handle.localRow);
        return 1;
    }
    WorldFieldDefinition field{};
    if (!resolved_field(state, handle, key, field)) {
        return luaL_error(state, "generated-world row is stale");
    }
    push_field(state, field);
    return 1;
}

} // namespace

/** Lua index for a world row: dispatches to the reader for the row's own kind. */
int world_row_index(lua_State* state) {
    const auto* const handle =
        static_cast<const WorldRowHandle*>(luaL_checkudata(state, 1, kWorldRowMetatable));
    WorldRowDefinition row{};
    if (!resolved_row(state, *handle, row) || row.kind != handle->kind) {
        return luaL_error(state, "generated-world row is stale");
    }
    const std::string_view key = checked_key(state);
    switch (handle->kind) {
    case WorldCollectionKind::squadAnchors:
        return squad_anchor_index(state, *handle, row.squadAnchor, key);
    case WorldCollectionKind::authoredPlacements:
        return authored_index(state, *handle, row.authoredPlacement, key);
    case WorldCollectionKind::embeddedPlacements:
        return embedded_index(state, *handle, row.embeddedPlacement, key);
    case WorldCollectionKind::containerPlacements:
        return container_index(state, *handle, row.containerPlacement, key);
    case WorldCollectionKind::staticSpatialInstances:
        return static_spatial_index(state, *handle, row.staticSpatial, key);
    case WorldCollectionKind::triggerVolumes:
        return trigger_index(state, *handle, row.triggerVolume, key);
    default:
        return catalog_index(state, *handle, key);
    }
}

/** Lua index for a trigger vertex: its position lanes and id. */
int trigger_vertex_index(lua_State* state) {
    const auto* const handle = static_cast<const TriggerGeometryHandle*>(
        luaL_checkudata(state, 1, kTriggerVertexMetatable));
    Impl* const owner = impl_from_state(state);
    TriggerVertexDefinition row{};
    if (owner == nullptr || handle->kind != GeometryKind::vertices
        || !current(*owner, handle->generation)
        || !owner->definitions.world.resolveTriggerVertex(owner->definitions.world.context,
                                                          handle->generation,
                                                          handle->triggerRow,
                                                          handle->localRow,
                                                          row)) {
        return luaL_error(state, "trigger vertex is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "id") {
        push_geometry_id(state, handle->generation, handle->kind, row.row);
    } else if (key == "row") {
        lua_pushinteger(state, row.row);
    } else if (key == "local_row") {
        lua_pushinteger(state, row.localRow);
    } else if (key == "value") {
        push_vector(state, row.value);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for a trigger triangle: its corner indices and id. */
int trigger_triangle_index(lua_State* state) {
    const auto* const handle = static_cast<const TriggerGeometryHandle*>(
        luaL_checkudata(state, 1, kTriggerTriangleMetatable));
    Impl* const owner = impl_from_state(state);
    TriggerTriangleDefinition row{};
    if (owner == nullptr || handle->kind != GeometryKind::triangles
        || !current(*owner, handle->generation)
        || !owner->definitions.world.resolveTriggerTriangle(owner->definitions.world.context,
                                                            handle->generation,
                                                            handle->triggerRow,
                                                            handle->localRow,
                                                            row)) {
        return luaL_error(state, "trigger triangle is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "id") {
        push_geometry_id(state, handle->generation, handle->kind, row.row);
    } else if (key == "row") {
        lua_pushinteger(state, row.row);
    } else if (key == "local_row") {
        lua_pushinteger(state, row.localRow);
    } else if (key == "a") {
        lua_pushinteger(state, row.indices[0]);
    } else if (key == "b") {
        lua_pushinteger(state, row.indices[1]);
    } else if (key == "c") {
        lua_pushinteger(state, row.indices[2]);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

} // namespace sunrise::server::activity::mission::lua_vm::detail::world_api
