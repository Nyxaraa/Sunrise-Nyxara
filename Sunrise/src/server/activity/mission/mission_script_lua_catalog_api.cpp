#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

#include "mission_script_lua_catalog_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail::catalog_api {
namespace {

[[nodiscard]] std::string_view checked_key(lua_State* state) {
    std::size_t length = 0;
    const char* const value = luaL_checklstring(state, 2, &length);
    return {value, length};
}

/** Maps a Lua collection name to its catalog kind. @return False when the name is unknown. */
[[nodiscard]] bool collection_kind(std::string_view key, CatalogCollectionKind& output) noexcept {
    struct Entry final {
        std::string_view key;
        CatalogCollectionKind kind;
    };
    // Every runtime-pack collection a program may index, by its script-facing key.
    static constexpr std::array entries{
        Entry{"activities", CatalogCollectionKind::activities},
        Entry{"scenarios", CatalogCollectionKind::scenarios},
        Entry{"bubbles", CatalogCollectionKind::bubbles},
        Entry{"states", CatalogCollectionKind::states},
        Entry{"objects", CatalogCollectionKind::objects},
        Entry{"occurrences", CatalogCollectionKind::occurrences},
        Entry{"slots", CatalogCollectionKind::slots},
        Entry{"texts", CatalogCollectionKind::texts},
        Entry{"capabilities", CatalogCollectionKind::capabilities},
        Entry{"gates", CatalogCollectionKind::gates},
        Entry{"refusals", CatalogCollectionKind::refusals},
        Entry{"actor_classes", CatalogCollectionKind::actorClasses},
        Entry{"rsat_descriptors", CatalogCollectionKind::rsatDescriptors},
        Entry{"rsat_schemas", CatalogCollectionKind::rsatSchemas},
        Entry{"rsat_fields", CatalogCollectionKind::rsatFields},
        Entry{"squads", CatalogCollectionKind::squads},
        Entry{"squad_members", CatalogCollectionKind::squadMembers},
        Entry{"squad_anchors", CatalogCollectionKind::squadAnchors},
        Entry{"authored_scene_resources", CatalogCollectionKind::authoredSceneResources},
        Entry{"authored_scene_squad_edges", CatalogCollectionKind::authoredSceneSquadEdges},
        Entry{"activity_binding_locators", CatalogCollectionKind::activityBindingLocators},
    };
    const auto found = std::find_if(
        entries.begin(), entries.end(), [key](const Entry& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    output = found->kind;
    return true;
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

void push_catalog(lua_State* state, const CatalogDefinitionApi& api) {
    push_handle(state, kCatalogMetatable, CatalogHandle{api.generation});
}

void push_collection(lua_State* state,
                     const CatalogGenerationIdentity& generation,
                     CatalogCollectionKind kind,
                     std::uint32_t activityRow = 0,
                     bool activityOwned = false) {
    push_handle(state,
                kCatalogCollectionMetatable,
                CatalogCollectionHandle{generation, kind, activityRow, activityOwned});
}

void push_row(lua_State* state,
              const CatalogGenerationIdentity& generation,
              CatalogCollectionKind kind,
              std::uint32_t localRow) {
    push_handle(state, kCatalogRowMetatable, CatalogRowHandle{generation, kind, localRow});
}

void push_tag_collection(lua_State* state,
                         const CatalogGenerationIdentity& generation,
                         CatalogActivityOwnedKind kind,
                         std::uint32_t activityRow) {
    push_handle(state,
                kCatalogTagCollectionMetatable,
                CatalogTagCollectionHandle{generation, kind, activityRow});
}

/** Resolves the row range one catalog collection covers. @return False on a stale generation. */
[[nodiscard]] bool collection_range(lua_State* state,
                                    const CatalogCollectionHandle& collection,
                                    CatalogOwnedRangeDefinition& output) {
    output = {};
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, collection.generation)) {
        return false;
    }
    if (!collection.activityOwned) {
        const std::size_t count = owner->definitions.catalog.count(
            owner->definitions.catalog.context, collection.generation, collection.kind);
        if (count > (std::numeric_limits<std::uint32_t>::max)()) {
            return false;
        }
        output.count = static_cast<std::uint32_t>(count);
        return true;
    }
    return collection.kind == CatalogCollectionKind::activityBindingLocators
           && owner->definitions.catalog.resolveActivityOwnedRange(
               owner->definitions.catalog.context,
               collection.generation,
               collection.activityRow,
               CatalogActivityOwnedKind::bindingLocators,
               output);
}

/** Lua `at` for a catalog collection: resolves one 1-based row. */
[[nodiscard]] int catalog_collection_at(lua_State* state) {
    const auto* const collection = static_cast<const CatalogCollectionHandle*>(
        luaL_checkudata(state, 1, kCatalogCollectionMetatable));
    CatalogOwnedRangeDefinition range{};
    if (!collection_range(state, *collection, range)) {
        return luaL_error(state, "catalog collection is stale");
    }
    const lua_Integer selected = luaL_checkinteger(state, 2);
    if (selected <= 0 || static_cast<std::uint64_t>(selected) > range.count
        || range.firstIndex > (std::numeric_limits<std::uint32_t>::max)()
                                  - static_cast<std::uint32_t>(selected)) {
        return luaL_error(state, "catalog row is unavailable");
    }
    push_row(state,
             collection->generation,
             collection->kind,
             range.firstIndex + static_cast<std::uint32_t>(selected));
    return 1;
}

/** Lua index for a catalog collection: `count` and `at`, else nil. */
[[nodiscard]] int catalog_collection_index(lua_State* state) {
    const auto* const collection = static_cast<const CatalogCollectionHandle*>(
        luaL_checkudata(state, 1, kCatalogCollectionMetatable));
    CatalogOwnedRangeDefinition range{};
    if (!collection_range(state, *collection, range)) {
        return luaL_error(state, "catalog collection is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "count") {
        lua_pushinteger(state, range.count);
    } else if (key == "at") {
        lua_pushcfunction(state, &catalog_collection_at);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Maps a Lua key to its activity-owned kind, and says whether it is a tag collection. */
[[nodiscard]] bool activity_owned_kind(std::string_view key,
                                       CatalogActivityOwnedKind& output,
                                       bool& tagCollection) noexcept {
    tagCollection = true;
    if (key == "activity_root_candidate_tags") {
        output = CatalogActivityOwnedKind::activityRootCandidateTags;
    } else if (key == "scenario_name_candidate_tags") {
        output = CatalogActivityOwnedKind::scenarioNameCandidateTags;
    } else if (key == "evidence_root_tags") {
        output = CatalogActivityOwnedKind::evidenceRootTags;
    } else if (key == "binding_locators") {
        output = CatalogActivityOwnedKind::bindingLocators;
        tagCollection = false;
    } else {
        return false;
    }
    return true;
}

/** Lua index for a catalog row: named fields, plus the activity-owned collections. */
[[nodiscard]] int catalog_row_index(lua_State* state) {
    const auto* const handle =
        static_cast<const CatalogRowHandle*>(luaL_checkudata(state, 1, kCatalogRowMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "catalog row is stale");
    }
    const std::string_view key = checked_key(state);
    CatalogActivityOwnedKind ownedKind{};
    bool tagCollection = false;
    if (handle->kind == CatalogCollectionKind::activities
        && activity_owned_kind(key, ownedKind, tagCollection)) {
        CatalogOwnedRangeDefinition range{};
        if (!owner->definitions.catalog.resolveActivityOwnedRange(
                owner->definitions.catalog.context,
                handle->generation,
                handle->localRow,
                ownedKind,
                range)) {
            return luaL_error(state, "catalog activity-owned range is unavailable");
        }
        if (tagCollection) {
            push_tag_collection(state, handle->generation, ownedKind, handle->localRow);
        } else {
            push_collection(state,
                            handle->generation,
                            CatalogCollectionKind::activityBindingLocators,
                            handle->localRow,
                            true);
        }
        return 1;
    }
    CatalogFieldDefinition field{};
    if (!resolved_field(state, *handle, key, field)) {
        return luaL_error(state, "catalog row is stale");
    }
    push_field(state, field);
    return 1;
}

/** Resolves the row range one catalog tag collection covers. */
[[nodiscard]] bool tag_range(lua_State* state,
                             const CatalogTagCollectionHandle& collection,
                             CatalogOwnedRangeDefinition& output) {
    output = {};
    Impl* const owner = impl_from_state(state);
    return owner != nullptr && current(*owner, collection.generation)
           && owner->definitions.catalog.resolveActivityOwnedRange(
               owner->definitions.catalog.context,
               collection.generation,
               collection.activityRow,
               collection.kind,
               output);
}

/** Lua `at` for a catalog tag collection: resolves one 1-based tag. */
[[nodiscard]] int catalog_tag_collection_at(lua_State* state) {
    const auto* const collection = static_cast<const CatalogTagCollectionHandle*>(
        luaL_checkudata(state, 1, kCatalogTagCollectionMetatable));
    CatalogOwnedRangeDefinition range{};
    const lua_Integer selected = luaL_checkinteger(state, 2);
    if (!tag_range(state, *collection, range)) {
        return luaL_error(state, "catalog binding-tag collection is stale");
    }
    std::uint32_t tag = 0;
    Impl* const owner = impl_from_state(state);
    if (selected <= 0 || static_cast<std::uint64_t>(selected) > range.count || owner == nullptr
        || !owner->definitions.catalog.resolveActivityOwnedTag(owner->definitions.catalog.context,
                                                               collection->generation,
                                                               collection->activityRow,
                                                               collection->kind,
                                                               static_cast<std::uint32_t>(selected),
                                                               tag)) {
        return luaL_error(state, "catalog binding tag is unavailable");
    }
    lua_pushinteger(state, tag);
    return 1;
}

/** Lua index for a catalog tag collection: `count` and `at`, else nil. */
[[nodiscard]] int catalog_tag_collection_index(lua_State* state) {
    const auto* const collection = static_cast<const CatalogTagCollectionHandle*>(
        luaL_checkudata(state, 1, kCatalogTagCollectionMetatable));
    CatalogOwnedRangeDefinition range{};
    if (!tag_range(state, *collection, range)) {
        return luaL_error(state, "catalog binding-tag collection is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "count") {
        lua_pushinteger(state, range.count);
    } else if (key == "at") {
        lua_pushcfunction(state, &catalog_tag_collection_at);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Lua index for the catalog view: its collections and identity fields. */
[[nodiscard]] int catalog_index(lua_State* state) {
    const auto* const handle =
        static_cast<const CatalogHandle*>(luaL_checkudata(state, 1, kCatalogMetatable));
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, handle->generation)) {
        return luaL_error(state, "catalog view is stale");
    }
    const std::string_view key = checked_key(state);
    if (key == "sdk_build_sha256") {
        push_digest(state, handle->generation.sdkBuildSha256);
    } else if (key == "sdk_payload_sha256") {
        push_digest(state, handle->generation.sdkPayloadSha256);
    } else if (key == "content_key_sha256") {
        push_digest(state, handle->generation.contentKeySha256);
    } else if (key == "logical_ir_sha256") {
        push_digest(state, handle->generation.logicalIrSha256);
    } else if (key == "activity_client_generation") {
        std::array<char, 32> text{};
        const auto value = std::to_chars(
            text.data(), text.data() + text.size(), handle->generation.activityClientGeneration);
        if (value.ec != std::errc{}) {
            return luaL_error(state, "catalog generation conversion failed");
        }
        lua_pushlstring(state, text.data(), static_cast<std::size_t>(value.ptr - text.data()));
    } else if (key == "activity_row") {
        if (handle->generation.activityRow == (std::numeric_limits<std::uint32_t>::max)()) {
            return luaL_error(state, "catalog activity row is unavailable");
        }
        lua_pushinteger(state, static_cast<lua_Integer>(handle->generation.activityRow) + 1);
    } else if (key == "scenario_row") {
        if (handle->generation.scenarioRow == (std::numeric_limits<std::uint32_t>::max)()) {
            return luaL_error(state, "catalog scenario row is unavailable");
        }
        lua_pushinteger(state, static_cast<lua_Integer>(handle->generation.scenarioRow) + 1);
    } else if (key == "definition_hash") {
        lua_pushinteger(state, handle->generation.definitionHash);
    } else if (key == "scenario_tag") {
        lua_pushinteger(state, handle->generation.scenarioTag);
    } else if (CatalogCollectionKind kind{}; collection_kind(key, kind)) {
        push_collection(state, handle->generation, kind);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

} // namespace

bool current(const Impl& owner, const CatalogGenerationIdentity& generation) noexcept {
    const CatalogDefinitionApi& api = owner.definitions.catalog;
    return api.context != nullptr && api.validate != nullptr && api.count != nullptr
           && api.resolveField != nullptr && api.resolveActivityOwnedRange != nullptr
           && api.resolveActivityOwnedTag != nullptr && api.generation == generation
           && api.validate(api.context, generation);
}

/** Resolves one named field of a catalog row. @return False when the generation is stale. */
bool resolved_field(lua_State* state,
                    const CatalogRowHandle& handle,
                    std::string_view key,
                    CatalogFieldDefinition& output) {
    Impl* const owner = impl_from_state(state);
    return owner != nullptr && current(*owner, handle.generation)
           && owner->definitions.catalog.resolveField(owner->definitions.catalog.context,
                                                      handle.generation,
                                                      handle.kind,
                                                      handle.localRow,
                                                      key,
                                                      output);
}

/** Pushes one resolved catalog field, mapping its kind to the matching Lua value. */
void push_field(lua_State* state, const CatalogFieldDefinition& value) {
    switch (value.kind) {
    case CatalogFieldKind::absent:
        lua_pushnil(state);
        return;
    case CatalogFieldKind::unsignedInteger:
        lua_pushinteger(state, static_cast<lua_Integer>(value.unsignedValue));
        return;
    case CatalogFieldKind::signedInteger:
        lua_pushinteger(state, static_cast<lua_Integer>(value.signedValue));
        return;
    case CatalogFieldKind::unsignedDecimalString: {
        std::array<char, 32> text{};
        const auto result =
            std::to_chars(text.data(), text.data() + text.size(), value.unsignedValue);
        if (result.ec != std::errc{}) {
            luaL_error(state, "catalog unsigned value conversion failed");
        }
        lua_pushlstring(state, text.data(), static_cast<std::size_t>(result.ptr - text.data()));
        return;
    }
    case CatalogFieldKind::signedDecimalString: {
        std::array<char, 32> text{};
        const auto result =
            std::to_chars(text.data(), text.data() + text.size(), value.signedValue);
        if (result.ec != std::errc{}) {
            luaL_error(state, "catalog signed value conversion failed");
        }
        lua_pushlstring(state, text.data(), static_cast<std::size_t>(result.ptr - text.data()));
        return;
    }
    case CatalogFieldKind::string:
        lua_pushlstring(state, value.stringValue.data(), value.stringValue.size());
        return;
    case CatalogFieldKind::bytes: {
        // Lowercase hex digits; every digest and byte field is spelled this way.
        constexpr char digits[] = "0123456789abcdef";
        std::array<char, 80> text{};
        if (value.valueCount > value.bytesValue.size()) {
            luaL_error(state, "catalog byte field exceeds its declared bound");
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

void register_metatables(lua_State* state) {
    register_metatable(state, kCatalogMetatable, &catalog_index);
    register_metatable(state, kCatalogCollectionMetatable, &catalog_collection_index);
    register_metatable(state, kCatalogRowMetatable, &catalog_row_index);
    register_metatable(state, kCatalogTagCollectionMetatable, &catalog_tag_collection_index);
}

/** Serves the activity table's `catalog` member. @return False for any other key. */
bool push_activity_member(lua_State* state, std::string_view key) {
    if (key != "catalog") {
        return false;
    }
    Impl* const owner = impl_from_state(state);
    if (owner == nullptr || !current(*owner, owner->definitions.catalog.generation)) {
        lua_pushnil(state);
        return true;
    }
    push_catalog(state, owner->definitions.catalog);
    return true;
}

} // namespace sunrise::server::activity::mission::lua_vm::detail::catalog_api
