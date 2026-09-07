#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>

#include "mission_script_vm_internal.h"

// Named effect arguments. Every effect takes one table, so no call site can get an order wrong.
// A parameter type carries its own bound, so reading one needs no range test here.

namespace sunrise::server::activity::mission::lua_vm::detail {

/** Stack index of the one argument table every effect takes. */
inline constexpr int kArgumentTable = 2;

/**
 * Pushes one field of the argument table.
 * A raw read, so a metatable a script installed on its own table reaches nothing.
 * @return The Lua type pushed, LUA_TNIL when no table was passed.
 */
[[nodiscard]] inline int push_argument(lua_State* state, const char* name) {
    if (lua_type(state, kArgumentTable) != LUA_TTABLE) {
        lua_pushnil(state);
        return LUA_TNIL;
    }
    lua_pushstring(state, name);
    return lua_rawget(state, kArgumentTable);
}

/**
 * Refuses an argument table carrying a key the endpoint does not declare.
 * A missing required key is refused by its own reader, which names it.
 * @param names Every key the endpoint declares, required and optional alike.
 */
inline void refuse_unknown_arguments(lua_State* state, std::span<const std::string_view> names) {
    if (lua_isnoneornil(state, kArgumentTable)) {
        return;
    }
    if (lua_type(state, kArgumentTable) != LUA_TTABLE) {
        static_cast<void>(luaL_error(state, "effect arguments must be one table"));
        return;
    }
    lua_pushnil(state);
    while (lua_next(state, kArgumentTable) != 0) {
        std::string_view key{};
        // Reading a non-string key as a string would rewrite it in place and break lua_next.
        if (lua_type(state, -2) == LUA_TSTRING) {
            std::size_t length = 0;
            const char* const text = lua_tolstring(state, -2, &length);
            key = {text, length};
        }
        if (std::find(names.begin(), names.end(), key) == names.end()) {
            static_cast<void>(luaL_error(state, "effect argument is not declared"));
            return;
        }
        lua_pop(state, 1);
    }
}

/**
 * Reads one required handle argument by value.
 * @param name Field name in the argument table.
 * @param metatable Metatable the value must carry.
 * @return The handle. The call errors when the field is absent or carries another type.
 */
template <typename Handle>
[[nodiscard]] Handle checked_argument(lua_State* state, const char* name, const char* metatable) {
    static_cast<void>(push_argument(state, name));
    const void* const value = luaL_testudata(state, -1, metatable);
    if (value != nullptr) {
        const Handle handle = *static_cast<const Handle*>(value);
        lua_pop(state, 1);
        return handle;
    }
    luaL_error(state, "%s must be a %s", name, metatable);
    // luaL_error never returns; this satisfies the return contract.
    return Handle{};
}

/**
 * Reads one optional handle argument by value.
 * @param output Written only when the field is present.
 * @return True when the field was present. The call errors when it carries another type.
 */
template <typename Handle>
[[nodiscard]] bool
optional_argument(lua_State* state, const char* name, const char* metatable, Handle& output) {
    if (push_argument(state, name) == LUA_TNIL) {
        lua_pop(state, 1);
        return false;
    }
    const void* const value = luaL_testudata(state, -1, metatable);
    if (value == nullptr) {
        return luaL_error(state, "%s must be a %s", name, metatable) != 0;
    }
    output = *static_cast<const Handle*>(value);
    lua_pop(state, 1);
    return true;
}

/**
 * Reads one optional boolean argument.
 * @param fallback Value used when the field is absent.
 * @return The field, or the fallback. The call errors when the field carries another type.
 */
[[nodiscard]] inline bool
optional_boolean_argument(lua_State* state, const char* name, bool fallback) {
    if (push_argument(state, name) == LUA_TNIL) {
        lua_pop(state, 1);
        return fallback;
    }
    if (!lua_isboolean(state, -1)) {
        return luaL_error(state, "%s must be a boolean", name) != 0;
    }
    const bool value = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return value;
}

/**
 * Reads one required string argument and leaves it on the stack.
 * The returned view borrows the Lua string, so the caller must keep that slot until it is done
 * and pop it afterwards.
 * @return The field. The call errors when it is absent or is not a string.
 */
[[nodiscard]] inline std::string_view borrowed_string_argument(lua_State* state, const char* name) {
    static_cast<void>(push_argument(state, name));
    if (lua_type(state, -1) != LUA_TSTRING) {
        static_cast<void>(luaL_error(state, "%s must be a string", name));
        return {};
    }
    std::size_t length = 0;
    const char* const value = lua_tolstring(state, -1, &length);
    return {value, length};
}

/**
 * Reads one required integer argument.
 * @return The field. The call errors when it is absent or is not an integer.
 */
[[nodiscard]] inline lua_Integer checked_integer_argument(lua_State* state, const char* name) {
    static_cast<void>(push_argument(state, name));
    if (!lua_isinteger(state, -1)) {
        return luaL_error(state, "%s must be an integer", name);
    }
    const lua_Integer value = lua_tointeger(state, -1);
    lua_pop(state, 1);
    return value;
}

/** Reads one optional integer argument and uses the native neutral value when absent. */
[[nodiscard]] inline lua_Integer
optional_integer_argument(lua_State* state, const char* name, lua_Integer fallback) {
    if (push_argument(state, name) == LUA_TNIL) {
        lua_pop(state, 1);
        return fallback;
    }
    if (!lua_isinteger(state, -1)) {
        return luaL_error(state, "%s must be an integer", name);
    }
    const lua_Integer value = lua_tointeger(state, -1);
    lua_pop(state, 1);
    return value;
}

/** Reads one required numeric argument without imposing a semantic range. */
[[nodiscard]] inline lua_Number checked_number_argument(lua_State* state, const char* name) {
    static_cast<void>(push_argument(state, name));
    if (!lua_isnumber(state, -1)) {
        return static_cast<lua_Number>(luaL_error(state, "%s must be a number", name));
    }
    const lua_Number value = lua_tonumber(state, -1);
    lua_pop(state, 1);
    return value;
}

} // namespace sunrise::server::activity::mission::lua_vm::detail
