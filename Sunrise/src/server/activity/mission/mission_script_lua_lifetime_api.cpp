#include <array>
#include <string_view>

#include "mission_script_lua_internal.h"
#include "mission_script_lua_names.h"
#include "mission_script_lua_types.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

/** Reads the one authorized action on the activity lifetime handle. */
[[nodiscard]] int lifetime_index(lua_State* state) {
    static_cast<void>(luaL_checkudata(state, 1, kLifetimeMetatable));
    const std::string_view key = lua_string_view(state, 2);
    if (key == "set") {
        lua_pushcfunction(state, &lifetime_set);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Sets the activity lifetime state the roster reports. Only a named state has a spelling. */
[[nodiscard]] int lifetime_set(lua_State* state) {
    static_cast<void>(luaL_checkudata(state, 1, kLifetimeMetatable));
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 1> kDeclared{"state"};
    refuse_unknown_arguments(state, kDeclared);
    const LifetimeStateHandle requested =
        checked_argument<LifetimeStateHandle>(state, "state", kLifetimeStateMetatable);
    CallFrame& frame = active_frame(state);
    Intent intent{};
    intent.kind = IntentKind::setLifetime;
    intent.lifetimeState = requested.state;
    return queue_intent(state, frame, intent);
}

void register_lifetime_metatables(lua_State* state) {
    register_metatable(state, kLifetimeMetatable, &lifetime_index);
}

} // namespace sunrise::server::activity::mission::lua_vm::detail
