#pragma once

#include <cstdint>
#include <string_view>

#include "mission_script_lua_types.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail::catalog_api {

// Metatable names that lock the runtime-pack catalog userdata shapes a program may hold.
inline constexpr char kCatalogMetatable[] = "sunrise.sdk.catalog";
inline constexpr char kCatalogCollectionMetatable[] = "sunrise.sdk.catalog_collection";
inline constexpr char kCatalogRowMetatable[] = "sunrise.sdk.catalog_row";
inline constexpr char kCatalogTagCollectionMetatable[] = "sunrise.sdk.catalog_tag_collection";

struct CatalogHandle final {
    CatalogGenerationIdentity generation{};
};

struct CatalogCollectionHandle final {
    CatalogGenerationIdentity generation{};
    CatalogCollectionKind kind{CatalogCollectionKind::activities};
    std::uint32_t activityRow{};
    bool activityOwned{};
};

struct CatalogRowHandle final {
    CatalogGenerationIdentity generation{};
    CatalogCollectionKind kind{CatalogCollectionKind::activities};
    std::uint32_t localRow{};
};

struct CatalogTagCollectionHandle final {
    CatalogGenerationIdentity generation{};
    CatalogActivityOwnedKind kind{CatalogActivityOwnedKind::activityRootCandidateTags};
    std::uint32_t activityRow{};
};

[[nodiscard]] bool current(const Impl& owner, const CatalogGenerationIdentity& generation) noexcept;
[[nodiscard]] bool resolved_field(lua_State* state,
                                  const CatalogRowHandle& handle,
                                  std::string_view key,
                                  CatalogFieldDefinition& output);
void push_field(lua_State* state, const CatalogFieldDefinition& value);

/** Registers every locked complete-catalog userdata shape. */
void register_metatables(lua_State* state);
/** Pushes one recognized ActivityView catalog member and returns whether the key was owned. */
[[nodiscard]] bool push_activity_member(lua_State* state, std::string_view key);

} // namespace sunrise::server::activity::mission::lua_vm::detail::catalog_api
