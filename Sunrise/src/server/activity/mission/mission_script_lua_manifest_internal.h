#pragma once

#include <cstdint>
#include <string_view>

#include "mission_script_lua_types.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail::manifest_api {

// Metatable names that lock the manifest userdata shapes a program may hold.
inline constexpr char kManifestMetatable[] = "sunrise.sdk.manifest";
inline constexpr char kManifestCollectionMetatable[] = "sunrise.sdk.manifest_collection";
inline constexpr char kManifestRowMetatable[] = "sunrise.sdk.manifest_row";
inline constexpr char kManifestTagCollectionMetatable[] = "sunrise.sdk.manifest_tag_collection";
inline constexpr char kManifestLocatorCollectionMetatable[] =
    "sunrise.sdk.manifest_locator_collection";
// One binding locator row of a manifest variant.
inline constexpr char kManifestLocatorMetatable[] = "sunrise.sdk.manifest_locator";

struct ManifestHandle final {
    WorldGenerationIdentity generation{};
};

struct ManifestCollectionHandle final {
    WorldGenerationIdentity generation{};
    ManifestCollectionKind kind{ManifestCollectionKind::scenarios};
};

struct ManifestRowHandle final {
    WorldGenerationIdentity generation{};
    ManifestCollectionKind kind{ManifestCollectionKind::scenarios};
    std::uint32_t row{};
};

struct ManifestTagCollectionHandle final {
    WorldGenerationIdentity generation{};
    ManifestVariantTagKind kind{ManifestVariantTagKind::activityRootCandidates};
    std::uint32_t variantRow{};
};

struct ManifestLocatorCollectionHandle final {
    WorldGenerationIdentity generation{};
    std::uint32_t variantRow{};
};

struct ManifestLocatorHandle final {
    WorldGenerationIdentity generation{};
    std::uint32_t variantRow{};
    std::uint32_t localRow{};
};

/** Registers every locked manifest userdata shape. */
void register_metatables(lua_State* state);
/** Pushes context.sdk.manifest and returns whether the key was owned. */
[[nodiscard]] bool push_activity_member(lua_State* state, std::string_view key);

} // namespace sunrise::server::activity::mission::lua_vm::detail::manifest_api
