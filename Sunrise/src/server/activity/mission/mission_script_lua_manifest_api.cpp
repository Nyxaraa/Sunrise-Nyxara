#include <array>
#include <charconv>
#include <limits>

#include "../../../state/activity_sdk/generated_world/catalog_manifest.h"
#include "mission_script_lua_manifest_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail::manifest_api {
namespace {

namespace manifest = state::activity_sdk::generated_world::manifest;

[[nodiscard]] std::string_view checked_key(lua_State* state) {
    std::size_t length = 0;
    const char* const value = luaL_checklstring(state, 2, &length);
    return {value, length};
}

[[nodiscard]] bool current(const Impl& owner, const WorldGenerationIdentity& generation) noexcept {
    const ManifestDefinitionApi& api = owner.definitions.manifest;
    return api.context != nullptr && api.validate != nullptr && api.count != nullptr
           && api.resolveField != nullptr && api.variantTagCount != nullptr
           && api.resolveVariantTag != nullptr && api.variantLocatorCount != nullptr
           && api.resolveVariantLocator != nullptr && api.generation == generation
           && api.validate(api.context, generation);
}

/** Pushes one 32-byte digest as 64 lowercase hex characters. */
void push_digest(lua_State* state, const std::array<std::byte, 32>& value) {
    // Lowercase hex digits; every digest and byte field is spelled this way.
    constexpr char digits[] = "0123456789abcdef";
    std::array<char, 64> text{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto byte = static_cast<std::uint8_t>(value[index]);
        text[index * 2U] = digits[byte >> 4U];
        text[index * 2U + 1U] = digits[byte & 0xFU];
    }
    lua_pushlstring(state, text.data(), text.size());
}

void push_u64_text(lua_State* state, std::uint64_t value) {
    std::array<char, 32> text{};
    const auto result = std::to_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{}) {
        luaL_error(state, "manifest value conversion failed");
    }
    lua_pushlstring(state, text.data(), static_cast<std::size_t>(result.ptr - text.data()));
}

/** Pushes one resolved manifest field, mapping its kind to the matching Lua value. */
void push_field(lua_State* state, const ManifestFieldDefinition& value) {
    switch (value.kind) {
    case ManifestFieldKind::absent:
        lua_pushnil(state);
        return;
    case ManifestFieldKind::unsignedInteger:
        if (value.unsignedValue
            > static_cast<std::uint64_t>((std::numeric_limits<lua_Integer>::max)())) {
            push_u64_text(state, value.unsignedValue);
        } else {
            lua_pushinteger(state, static_cast<lua_Integer>(value.unsignedValue));
        }
        return;
    case ManifestFieldKind::unsignedDecimalString:
        push_u64_text(state, value.unsignedValue);
        return;
    case ManifestFieldKind::boolean:
        lua_pushboolean(state, value.unsignedValue != 0);
        return;
    case ManifestFieldKind::string:
        lua_pushlstring(state, value.stringValue.data(), value.stringValue.size());
        return;
    case ManifestFieldKind::bytes:
        if (value.valueCount != value.bytesValue.size()) {
            luaL_error(state, "manifest digest has an invalid width");
        }
        push_digest(state, value.bytesValue);
        return;
    }
    lua_pushnil(state);
}

void push_manifest(lua_State* state, const ManifestDefinitionApi& api) {
    push_handle(state, kManifestMetatable, ManifestHandle{api.generation});
}

void push_collection(lua_State* state,
                     const WorldGenerationIdentity& generation,
                     ManifestCollectionKind kind) {
    push_handle(state, kManifestCollectionMetatable, ManifestCollectionHandle{generation, kind});
}

void push_row(lua_State* state,
              const WorldGenerationIdentity& generation,
              ManifestCollectionKind kind,
              std::uint32_t row) {
    push_handle(state, kManifestRowMetatable, ManifestRowHandle{generation, kind, row});
}

void push_tag_collection(lua_State* state,
                         const WorldGenerationIdentity& generation,
                         ManifestVariantTagKind kind,
                         std::uint32_t variantRow) {
    push_handle(state,
                kManifestTagCollectionMetatable,
                ManifestTagCollectionHandle{generation, kind, variantRow});
}

void push_locator_collection(lua_State* state,
                             const WorldGenerationIdentity& generation,
                             std::uint32_t variantRow) {
    push_handle(state,
                kManifestLocatorCollectionMetatable,
                ManifestLocatorCollectionHandle{generation, variantRow});
}

/** Lua `at` for a manifest collection: resolves one 1-based row. */
[[nodiscard]] int collection_at(lua_State* state) {
    const auto* const handle = static_cast<const ManifestCollectionHandle*>(
        luaL_checkudata(state, 1, kManifestCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    const lua_Integer selected = luaL_checkinteger(state, 2);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "manifest collection is stale");
    }
    const std::size_t count = owner->definitions.manifest.count(
        owner->definitions.manifest.context, handle->generation, handle->kind);
    if (selected <= 0 || static_cast<std::uint64_t>(selected) > count) {
        return luaL_error(state, "manifest row is unavailable");
    }
    push_row(state, handle->generation, handle->kind, static_cast<std::uint32_t>(selected - 1));
    return 1;
}

/** Lua index for a manifest collection: `count` and `at`, else nil. */
[[nodiscard]] int collection_index(lua_State* state) {
    const auto* const handle = static_cast<const ManifestCollectionHandle*>(
        luaL_checkudata(state, 1, kManifestCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "manifest collection is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "count") {
        lua_pushinteger(
            state,
            static_cast<lua_Integer>(owner->definitions.manifest.count(
                owner->definitions.manifest.context, handle->generation, handle->kind)));
    } else if (key == "at") {
        lua_pushcfunction(state, &collection_at);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Maps a Lua key to its variant tag kind. @return False when the name is unknown. */
[[nodiscard]] bool variant_tag_kind(std::string_view key, ManifestVariantTagKind& output) noexcept {
    if (key == "activity_root_candidate_tags") {
        output = ManifestVariantTagKind::activityRootCandidates;
    } else if (key == "scenario_name_candidate_tags") {
        output = ManifestVariantTagKind::scenarioNameCandidates;
    } else if (key == "evidence_root_tags") {
        output = ManifestVariantTagKind::evidenceRoots;
    } else {
        return false;
    }
    return true;
}

/** Lua index for a manifest row: named fields, plus its tag and locator collections. */
[[nodiscard]] int row_index(lua_State* state) {
    const auto* const handle =
        static_cast<const ManifestRowHandle*>(luaL_checkudata(state, 1, kManifestRowMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "manifest row is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "row") {
        lua_pushinteger(state, static_cast<lua_Integer>(handle->row) + 1);
        return 1;
    }
    if (handle->kind == ManifestCollectionKind::activityVariants) {
        ManifestVariantTagKind tagKind{};
        if (variant_tag_kind(key, tagKind)) {
            push_tag_collection(state, handle->generation, tagKind, handle->row);
            return 1;
        }
        if (key == "binding_locators") {
            push_locator_collection(state, handle->generation, handle->row);
            return 1;
        }
    }
    ManifestFieldDefinition field{};
    if (!owner->definitions.manifest.resolveField(owner->definitions.manifest.context,
                                                  handle->generation,
                                                  handle->kind,
                                                  handle->row,
                                                  key,
                                                  field)) {
        return luaL_error(state, "manifest row is stale");
    }
    push_field(state, field);
    return 1;
}

/** Lua `at` for a manifest tag collection: resolves one 1-based tag. */
[[nodiscard]] int tag_collection_at(lua_State* state) {
    const auto* const handle = static_cast<const ManifestTagCollectionHandle*>(
        luaL_checkudata(state, 1, kManifestTagCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    const lua_Integer selected = luaL_checkinteger(state, 2);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "manifest tag collection is stale");
    }
    const std::size_t count = owner->definitions.manifest.variantTagCount(
        owner->definitions.manifest.context, handle->generation, handle->variantRow, handle->kind);
    std::uint32_t tag = 0;
    if (selected <= 0 || static_cast<std::uint64_t>(selected) > count
        || !owner->definitions.manifest.resolveVariantTag(owner->definitions.manifest.context,
                                                          handle->generation,
                                                          handle->variantRow,
                                                          handle->kind,
                                                          static_cast<std::uint32_t>(selected - 1),
                                                          tag)) {
        return luaL_error(state, "manifest tag is unavailable");
    }
    lua_pushinteger(state, tag);
    return 1;
}

/** Lua index for a manifest tag collection: `count` and `at`, else nil. */
[[nodiscard]] int tag_collection_index(lua_State* state) {
    const auto* const handle = static_cast<const ManifestTagCollectionHandle*>(
        luaL_checkudata(state, 1, kManifestTagCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "manifest tag collection is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "count") {
        lua_pushinteger(state,
                        static_cast<lua_Integer>(owner->definitions.manifest.variantTagCount(
                            owner->definitions.manifest.context,
                            handle->generation,
                            handle->variantRow,
                            handle->kind)));
    } else if (key == "at") {
        lua_pushcfunction(state, &tag_collection_at);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua `at` for a manifest locator collection: resolves one 1-based locator. */
[[nodiscard]] int locator_collection_at(lua_State* state) {
    const auto* const handle = static_cast<const ManifestLocatorCollectionHandle*>(
        luaL_checkudata(state, 1, kManifestLocatorCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    const lua_Integer selected = luaL_checkinteger(state, 2);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "manifest locator collection is stale");
    }
    const std::size_t count = owner->definitions.manifest.variantLocatorCount(
        owner->definitions.manifest.context, handle->generation, handle->variantRow);
    if (selected <= 0 || static_cast<std::uint64_t>(selected) > count) {
        return luaL_error(state, "manifest locator is unavailable");
    }
    push_handle(state,
                kManifestLocatorMetatable,
                ManifestLocatorHandle{handle->generation,
                                      handle->variantRow,
                                      static_cast<std::uint32_t>(selected - 1)});
    return 1;
}

/** Lua index for a manifest locator collection: `count` and `at`, else nil. */
[[nodiscard]] int locator_collection_index(lua_State* state) {
    const auto* const handle = static_cast<const ManifestLocatorCollectionHandle*>(
        luaL_checkudata(state, 1, kManifestLocatorCollectionMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "manifest locator collection is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "count") {
        lua_pushinteger(
            state,
            static_cast<lua_Integer>(owner->definitions.manifest.variantLocatorCount(
                owner->definitions.manifest.context, handle->generation, handle->variantRow)));
    } else if (key == "at") {
        lua_pushcfunction(state, &locator_collection_at);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for one payload locator: its package tag and byte offset. */
[[nodiscard]] int locator_index(lua_State* state) {
    const auto* const handle = static_cast<const ManifestLocatorHandle*>(
        luaL_checkudata(state, 1, kManifestLocatorMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "manifest locator is stale");
    }
    ManifestLocatorDefinition locator{};
    if (!owner->definitions.manifest.resolveVariantLocator(owner->definitions.manifest.context,
                                                           handle->generation,
                                                           handle->variantRow,
                                                           handle->localRow,
                                                           locator)) {
        return luaL_error(state, "manifest locator is unavailable");
    }
    const std::string_view key = checked_key(state);
    if (key == "row") {
        lua_pushinteger(state, locator.localRow);
    } else if (key == "tag") {
        lua_pushinteger(state, locator.tag);
    } else if (key == "offset") {
        push_u64_text(state, locator.offset);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Maps a Lua collection name to its manifest kind. @return False when the name is unknown. */
[[nodiscard]] bool collection_kind(std::string_view key, ManifestCollectionKind& output) noexcept {
    if (key == "scenarios") {
        output = ManifestCollectionKind::scenarios;
    } else if (key == "activity_roots") {
        output = ManifestCollectionKind::activityRoots;
    } else if (key == "activity_variants") {
        output = ManifestCollectionKind::activityVariants;
    } else {
        return false;
    }
    return true;
}

/** Lua index for the manifest view: its collections and identity fields. */
[[nodiscard]] int manifest_index(lua_State* state) {
    const auto* const handle =
        static_cast<const ManifestHandle*>(luaL_checkudata(state, 1, kManifestMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "manifest view is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "format_version") {
        lua_pushinteger(state, manifest::kVersion);
    } else if (key == "source_fingerprint") {
        push_digest(state, handle->generation.sourceFingerprint);
    } else if (key == "sdk_build_sha256") {
        push_digest(state, handle->generation.sdkBuildSha256);
    } else if (key == "sdk_payload_sha256") {
        push_digest(state, handle->generation.sdkPayloadSha256);
    } else if (key == "manifest_payload_sha256") {
        push_digest(state, handle->generation.manifestPayloadSha256);
    } else if (key == "shard_payload_sha256") {
        push_digest(state, handle->generation.shardPayloadSha256);
    } else if (key == "activity_client_generation") {
        push_u64_text(state, handle->generation.activityClientGeneration);
    } else if (key == "activity_row") {
        lua_pushinteger(state, static_cast<lua_Integer>(handle->generation.activityRow) + 1);
    } else if (key == "scenario_tag") {
        lua_pushinteger(state, handle->generation.scenarioTag);
    } else if (key == "binding_completeness") {
        push_row(state, handle->generation, ManifestCollectionKind::bindingCompleteness, 0);
    } else if (ManifestCollectionKind kind{}; collection_kind(key, kind)) {
        push_collection(state, handle->generation, kind);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

} // namespace

void register_metatables(lua_State* state) {
    register_metatable(state, kManifestMetatable, &manifest_index);
    register_metatable(state, kManifestCollectionMetatable, &collection_index);
    register_metatable(state, kManifestRowMetatable, &row_index);
    register_metatable(state, kManifestTagCollectionMetatable, &tag_collection_index);
    register_metatable(state, kManifestLocatorCollectionMetatable, &locator_collection_index);
    register_metatable(state, kManifestLocatorMetatable, &locator_index);
}

/** Serves the activity table's `manifest` member. @return False for any other key. */
bool push_activity_member(lua_State* state, std::string_view key) {
    if (key != "manifest") {
        return false;
    }
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, owner->definitions.manifest.generation)) {
        lua_pushnil(state);
        return true;
    }
    push_manifest(state, owner->definitions.manifest);
    return true;
}

} // namespace sunrise::server::activity::mission::lua_vm::detail::manifest_api
