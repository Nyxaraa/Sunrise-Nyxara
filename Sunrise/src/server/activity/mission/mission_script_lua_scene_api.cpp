#include <string_view>

#include "mission_script_lua_internal.h"
#include "mission_script_lua_names.h"
#include "mission_script_lua_resolve.h"
#include "mission_script_lua_types.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

/** Lua index for a scene handle: its named fields. Errors when the scene is stale. */
[[nodiscard]] int scene_index(lua_State* state) {
    const auto* const handle =
        static_cast<const SceneHandle*>(luaL_checkudata(state, 1, kSceneMetatable));
    SceneDefinition definition{};
    if (!current_scene(state, *handle, definition)) {
        return luaL_error(state, "authored scene is stale");
    }
    const std::string_view key = lua_string_view(state, 2);
    if (key == "row") {
        lua_pushinteger(state, definition.localRow);
    } else if (key == "id") {
        lua_pushlstring(state, definition.id.data(), definition.idLength);
    } else if (key == "activate") {
        lua_pushcfunction(state, &scene_activate);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Stages one authored-scene activation. scene:activate{} reads no parameters. */
[[nodiscard]] int scene_activate(lua_State* state) {
    const auto* const handle =
        static_cast<const SceneHandle*>(luaL_checkudata(state, 1, kSceneMetatable));
    SceneDefinition definition{};
    if (!current_scene(state, *handle, definition)) {
        return luaL_error(state, "authored scene is stale or invalid");
    }
    refuse_unknown_arguments(state, {});
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::activateAuthoredScene;
    intent.firstRow = definition.occurrenceRow;
    intent.secondRow = definition.slotRow;
    return queue_intent(state, frame, intent);
}

void register_scene_metatables(lua_State* state) {
    register_metatable(state, kSceneMetatable, &scene_index);
}

} // namespace sunrise::server::activity::mission::lua_vm::detail
