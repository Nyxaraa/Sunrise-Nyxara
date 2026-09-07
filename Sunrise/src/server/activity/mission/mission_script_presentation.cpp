#include "mission_script_presentation.h"
#ifdef SUNRISE_LUA_C_LINKAGE
#include "lua.hpp"
#else
#include "lauxlib.h"
#include "lua.h"
#endif
#include "mission_script_vm_internal.h"
#include <limits>
#include <string_view>

#include "../../../state/activity/presentation/config.h"
#include "../../../state/activity/presentation/config_rules.h"

namespace sunrise::server::activity::mission::lua_vm::detail {
namespace {
void fields(lua_State* state, int table, std::initializer_list<std::string_view> allowed) {
    lua_pushnil(state);
    while (lua_next(state, table)) {
        if (lua_type(state, -2) != LUA_TSTRING)
            luaL_error(state, "presentation fields must be named");
        std::size_t size{};
        const char* name = lua_tolstring(state, -2, &size);
        bool known = false;
        for (auto field : allowed)
            known |= field == std::string_view(name, size);
        if (!known) luaL_error(state, "unknown presentation field: %s", name);
        lua_pop(state, 1);
    }
}
std::uint32_t integer(lua_State* state, int table, const char* name, bool zero = false) {
    lua_getfield(state, table, name);
    const auto value = luaL_checkinteger(state, -1);
    if (value < (zero ? 0 : 1) || static_cast<std::uint64_t>(value) >= 0xFFFFFFFFULL)
        luaL_error(state, "presentation.%s is outside its unsigned range", name);
    lua_pop(state, 1);
    return static_cast<std::uint32_t>(value);
}
void array_keys(lua_State* state, int table, std::size_t size) {
    lua_pushnil(state);
    while (lua_next(state, table)) {
        if (!lua_isinteger(state, -2))
            luaL_error(state, "presentation arrays must use integer keys");
        const auto key = lua_tointeger(state, -2);
        if (key < 1 || static_cast<std::uint64_t>(key) > size)
            luaL_error(state, "presentation arrays must be dense");
        lua_pop(state, 1);
    }
}
template <class Rows, class Read>
unsigned rows(lua_State* state, int table, const char* name, Rows& output, Read read) {
    lua_getfield(state, table, name);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return 0;
    }
    luaL_checktype(state, -1, LUA_TTABLE);
    const auto size = lua_rawlen(state, -1);
    if (size > output.size()) luaL_error(state, "presentation.%s has invalid size", name);
    array_keys(state, lua_gettop(state), size);
    for (std::size_t i = 0; i < size; ++i) {
        lua_rawgeti(state, -1, i + 1);
        luaL_checktype(state, -1, LUA_TTABLE);
        read(lua_gettop(state), output[i]);
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    return static_cast<unsigned>(size);
}
void surface_array(lua_State* state,
                   int table,
                   const char* name,
                   std::array<std::uint32_t, 6>& values) {
    lua_getfield(state, table, name);
    luaL_checktype(state, -1, LUA_TTABLE);
    if (lua_rawlen(state, -1) != values.size())
        luaL_error(state, "movie surfaces require six entries");
    array_keys(state, lua_gettop(state), values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        lua_rawgeti(state, -1, i + 1);
        const auto value = luaL_checkinteger(state, -1);
        if (value <= 0 || static_cast<std::uint64_t>(value) >= 0xFFFFFFFFULL)
            luaL_error(state, "invalid surface tag");
        values[i] = static_cast<std::uint32_t>(value);
        for (std::size_t j = 0; j < i; ++j)
            if (values[j] == values[i]) luaL_error(state, "duplicate surface tag");
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
}
Impl& setup_owner(lua_State* state) {
    auto* impl = impl_from_state(state);
    if (!impl || impl->active || impl->identity.publicTarget)
        luaL_error(state, "presentation setup is only available while loading a private mission");
    return *impl;
}
int suppress_loading(lua_State* state) {
    auto& impl = setup_owner(state);
    luaL_checktype(state, 1, LUA_TBOOLEAN);
    impl.presentation.suppressLoadingCinematics = lua_toboolean(state, 1);
    impl.presentationConfigured = true;
    return 0;
}
int mask_loading_screen(lua_State* state) {
    auto& impl = setup_owner(state);
    luaL_checktype(state, 1, LUA_TBOOLEAN);
    impl.presentation.maskLoadingScreen = lua_toboolean(state, 1);
    impl.presentationConfigured = true;
    return 0;
}
// Reuse the declaration parser so both entry points enforce identical resource bounds.
int configure_renderer(lua_State* state) {
    auto& impl = setup_owner(state);
    luaL_checktype(state, 1, LUA_TTABLE);
    fields(state, 1, {"movies", "surfaces"});
    lua_newtable(state);
    const int program = lua_gettop(state);
    lua_pushvalue(state, 1);
    lua_setfield(state, program, "presentation");
    state::activity::presentation::Config parsed{};
    capture_presentation(state, program, false, parsed);
    parsed.suppressLoadingCinematics = impl.presentation.suppressLoadingCinematics;
    parsed.maskLoadingScreen = impl.presentation.maskLoadingScreen;
    impl.presentation = parsed;
    impl.presentationConfigured = true;
    return 0;
}
} // namespace
void capture_presentation(lua_State* state,
                          int program,
                          bool publicTarget,
                          state::activity::presentation::Config& config) {
    lua_getfield(state, program, "presentation");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    if (publicTarget) luaL_error(state, "local presentation requires a private activity");
    luaL_checktype(state, -1, LUA_TTABLE);
    const int table = lua_gettop(state);
    fields(state,
           table,
           {"suppress_loading_cinematics", "mask_loading_screen",
            "movies",
            "surfaces"});
    lua_getfield(state, table, "suppress_loading_cinematics");
    if (!lua_isnil(state, -1) && !lua_isboolean(state, -1))
        luaL_error(state, "suppression must be boolean");
    config.suppressLoadingCinematics = lua_toboolean(state, -1);
    lua_pop(state, 1);
    lua_getfield(state, table, "mask_loading_screen");
    if (!lua_isnil(state, -1) && !lua_isboolean(state, -1))
        luaL_error(state, "loading screen mask must be boolean");
    config.maskLoadingScreen = lua_toboolean(state, -1);
    lua_pop(state, 1);
    config.movieCount = rows(state, table, "movies", config.movies, [&](int row, auto& movie) {
        fields(state, row, {"asset", "header", "subtitles", "catalog", "stream"});
        movie = {integer(state, row, "asset"),
                 integer(state, row, "header"),
                 integer(state, row, "subtitles"),
                 integer(state, row, "catalog"),
                 integer(state, row, "stream")};
    });
    if (config.movieCount) {
        lua_getfield(state, table, "surfaces");
        luaL_checktype(state, -1, LUA_TTABLE);
        const int surfaces = lua_gettop(state);
        fields(state, surfaces, {"definitions", "buffers", "containers"});
        surface_array(state, surfaces, "definitions", config.surfaces.definitions);
        surface_array(state, surfaces, "buffers", config.surfaces.buffers);
        surface_array(state, surfaces, "containers", config.surfaces.containers);
        lua_pop(state, 1);
    }
    if (!state::activity::presentation::valid_config(config))
        luaL_error(state, "invalid or conflicting presentation declarations");
    lua_pop(state, 1);
}
} // namespace sunrise::server::activity::mission::lua_vm::detail

namespace sunrise::server::activity::mission::lua_vm::detail {
void register_presentation_api(lua_State* state) {
    lua_newtable(state);
    lua_newtable(state);
    constexpr luaL_Reg functions[]{
        {"suppress_loading_cinematics", suppress_loading},
        {"mask_loading_screen", mask_loading_screen},
        {"configure_movie_renderer", configure_renderer},
        {nullptr, nullptr},
    };
    luaL_setfuncs(state, functions, 0);
    lua_setfield(state, -2, "presentation");
    lua_setglobal(state, "sunrise");
}
} // namespace sunrise::server::activity::mission::lua_vm::detail
