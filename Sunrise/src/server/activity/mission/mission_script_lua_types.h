#pragma once

#include <cstdint>

#include "mission_script_lua_names.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

// --- Lua userdata shapes ----------------------------------------------------------------------
// A collection that addresses no row carries a marker so its storage is never zero-sized.

/** Handle for the running mission context. */
struct ContextHandle final {
    std::uint8_t marker{};
};

struct StateHandle final {
    std::uint8_t marker{};
};

struct LifetimeHandle final {
    std::uint8_t marker{};
};

struct EventHandle final {
    host::Event value{};
};

struct ActivityHandle final {
    std::uint8_t marker{};
};

struct ActivityBindingTagCollectionHandle final {
    ActivityBindingTagKind kind{ActivityBindingTagKind::activityRootCandidates};
};

struct ActivityBindingLocatorCollectionHandle final {
    std::uint8_t marker{};
};

struct ActivityBindingLocatorHandle final {
    std::uint32_t localRow{};
};

struct SquadCollectionHandle final {
    std::uint8_t marker{};
};

struct SceneCollectionHandle final {
    std::uint8_t marker{};
};

struct SlotCollectionHandle final {
    std::uint8_t marker{};
};

struct MessageCollectionHandle final {
    std::uint8_t marker{};
};

struct MessageFieldCollectionHandle final {
    std::uint32_t messageRow{};
};

struct SquadHandle final {
    std::uint32_t localRow{};
};

struct SceneHandle final {
    std::uint32_t localRow{};
};

/** One generated Slot row, addressed by its one-based catalog row. */
struct SlotHandle final {
    std::uint32_t localRow{};
};

/** One activity-message row, addressed by its one-based catalog row. */
struct MessageHandle final {
    std::uint32_t localRow{};
};

/** One field of one activity-message row. */
struct MessageFieldHandle final {
    std::uint32_t messageRow{};
    std::uint32_t localRow{};
};

/**
 * Pushes one locked userdata handle: zero user values and the named metatable, always both.
 * It is the only mission userdata constructor, and the shadow in mission_script_vm_internal.h
 * makes any hand-written Lua userdata call in this namespace fail to compile.
 */
template <typename Handle>
void push_handle(lua_State* state, const char* metatable, const Handle& value) {
    auto* const handle = static_cast<Handle*>(::lua_newuserdatauv(state, sizeof(Handle), 0));
    *handle = value;
    luaL_setmetatable(state, metatable);
}

/** Installs one hidden metatable. __metatable is false, so a script cannot read or replace it. */
inline void register_metatable(lua_State* state, const char* name, lua_CFunction index) {
    luaL_newmetatable(state, name);
    lua_pushcfunction(state, index);
    lua_setfield(state, -2, "__index");
    lua_pushboolean(state, 0);
    lua_setfield(state, -2, "__metatable");
    lua_pop(state, 1);
}

inline void push_activity(lua_State* state) {
    push_handle(state, kActivityMetatable, ActivityHandle{});
}

inline void push_lifetime(lua_State* state) {
    push_handle(state, kLifetimeMetatable, LifetimeHandle{});
}

inline void push_activity_binding_tag_collection(lua_State* state, ActivityBindingTagKind kind) {
    push_handle(
        state, kActivityBindingTagCollectionMetatable, ActivityBindingTagCollectionHandle{kind});
}

inline void push_activity_binding_locator_collection(lua_State* state) {
    push_handle(state,
                kActivityBindingLocatorCollectionMetatable,
                ActivityBindingLocatorCollectionHandle{});
}

inline void push_activity_binding_locator(lua_State* state, std::uint32_t localRow) {
    push_handle(state, kActivityBindingLocatorMetatable, ActivityBindingLocatorHandle{localRow});
}

inline void push_squad_collection(lua_State* state) {
    push_handle(state, kSquadCollectionMetatable, SquadCollectionHandle{});
}

inline void push_scene_collection(lua_State* state) {
    push_handle(state, kSceneCollectionMetatable, SceneCollectionHandle{});
}

inline void push_slot_collection(lua_State* state) {
    push_handle(state, kSlotCollectionMetatable, SlotCollectionHandle{});
}

inline void push_message(lua_State* state, std::uint32_t localRow) {
    push_handle(state, kMessageMetatable, MessageHandle{localRow});
}

inline void push_message_collection(lua_State* state) {
    push_handle(state, kMessageCollectionMetatable, MessageCollectionHandle{});
}

inline void push_message_field_collection(lua_State* state, std::uint32_t messageRow) {
    push_handle(state, kMessageFieldCollectionMetatable, MessageFieldCollectionHandle{messageRow});
}

inline void push_message_field(lua_State* state, std::uint32_t messageRow, std::uint32_t localRow) {
    push_handle(state, kMessageFieldMetatable, MessageFieldHandle{messageRow, localRow});
}

} // namespace sunrise::server::activity::mission::lua_vm::detail
