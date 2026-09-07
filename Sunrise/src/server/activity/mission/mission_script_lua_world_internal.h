#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "mission_script_lua_types.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail::world_api {

// Metatable names that lock the generated-world userdata shapes a program may hold.
inline constexpr char kWorldMetatable[] = "sunrise.sdk.world";
inline constexpr char kWorldCollectionMetatable[] = "sunrise.sdk.world_collection";
inline constexpr char kSquadAnchorCollectionMetatable[] = "sunrise.sdk.squad_anchors_owned";
inline constexpr char kWorldRowMetatable[] = "sunrise.sdk.world_row";
inline constexpr char kGeometryCollectionMetatable[] = "sunrise.sdk.trigger_geometry";
inline constexpr char kTriggerVertexMetatable[] = "sunrise.sdk.trigger_vertex";
inline constexpr char kTriggerTriangleMetatable[] = "sunrise.sdk.trigger_triangle";
inline constexpr char kVectorMetatable[] = "sunrise.sdk.vector";
inline constexpr char kWorldDiagnosticsMetatable[] = "sunrise.sdk.world_diagnostics";
inline constexpr char kWorldCoverageCollectionMetatable[] = "sunrise.sdk.world_coverage";
inline constexpr char kWorldCoverageMetatable[] = "sunrise.sdk.world_coverage_row";

struct WorldHandle final {
    WorldGenerationIdentity generation{};
};

struct WorldCollectionHandle final {
    WorldGenerationIdentity generation{};
    WorldCollectionKind kind{WorldCollectionKind::squadAnchors};
};

struct SquadAnchorCollectionHandle final {
    WorldGenerationIdentity generation{};
    std::uint32_t squadRow{};
};

struct WorldRowHandle final {
    WorldGenerationIdentity generation{};
    WorldCollectionKind kind{WorldCollectionKind::squadAnchors};
    std::uint32_t localRow{};
};

enum class GeometryKind : std::uint8_t {
    vertices,
    triangles,
};

struct GeometryCollectionHandle final {
    WorldGenerationIdentity generation{};
    GeometryKind kind{GeometryKind::vertices};
    std::uint32_t triggerRow{};
};

struct TriggerGeometryHandle final {
    WorldGenerationIdentity generation{};
    GeometryKind kind{GeometryKind::vertices};
    std::uint32_t triggerRow{};
    std::uint32_t localRow{};
};

struct VectorHandle final {
    std::array<float, 4> value{};
    std::uint8_t lanes{};
};

struct WorldDiagnosticsHandle final {
    WorldGenerationIdentity generation{};
};

struct WorldCoverageCollectionHandle final {
    WorldGenerationIdentity generation{};
};

struct WorldCoverageHandle final {
    WorldGenerationIdentity generation{};
    std::uint32_t localRow{};
};

[[nodiscard]] bool current(const Impl& owner, const WorldGenerationIdentity& generation) noexcept;
void push_vector(lua_State* state, std::span<const float> value);
void push_world_id(lua_State* state,
                   const WorldGenerationIdentity& generation,
                   WorldCollectionKind kind,
                   std::uint32_t row);
void push_geometry_collection(lua_State* state,
                              const WorldGenerationIdentity& generation,
                              GeometryKind kind,
                              std::uint32_t triggerRow);
void push_coverage_collection(lua_State* state, const WorldGenerationIdentity& generation);
void push_optional_row(lua_State* state, std::uint32_t value);
void push_optional_name(lua_State* state, std::string_view value);
void push_name_field(lua_State* state, std::string_view key, const WorldNameDefinition& value);
[[nodiscard]] bool
resolved_row(lua_State* state, const WorldRowHandle& handle, WorldRowDefinition& output);
[[nodiscard]] bool resolved_field(lua_State* state,
                                  const WorldRowHandle& handle,
                                  std::string_view key,
                                  WorldFieldDefinition& output);
void push_field(lua_State* state, const WorldFieldDefinition& value);

[[nodiscard]] int world_row_index(lua_State* state);
[[nodiscard]] int vector_index(lua_State* state);
[[nodiscard]] int trigger_vertex_index(lua_State* state);
[[nodiscard]] int trigger_triangle_index(lua_State* state);
[[nodiscard]] int world_coverage_collection_index(lua_State* state);
[[nodiscard]] int world_coverage_index(lua_State* state);
[[nodiscard]] int world_diagnostics_index(lua_State* state);

/** Registers every locked generated-world userdata shape. */
void register_metatables(lua_State* state);
/** Pushes one recognized ActivityView world member and returns whether the key was owned. */
[[nodiscard]] bool push_activity_member(lua_State* state, std::string_view key);
/** Pushes one recognized SquadView world member and returns whether the key was owned. */
[[nodiscard]] bool
push_squad_member(lua_State* state, const SquadDefinition& squad, std::string_view key);

} // namespace sunrise::server::activity::mission::lua_vm::detail::world_api
