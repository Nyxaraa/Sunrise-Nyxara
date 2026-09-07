#pragma once

#include <cstdint>

#include "mission_script_lua_types.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

// Metatable names that lock the BAP service userdata shapes a program may hold.
inline constexpr char kBapServiceCollectionMetatable[] = "sunrise.sdk.bap_services";
inline constexpr char kBapServiceMetatable[] = "sunrise.sdk.bap_service";

/** The collection userdata carries no row, so it holds only a presence marker. */
struct BapServiceCollectionHandle final {
    std::uint8_t marker{};
};

/** One service row, addressed by its one-based catalog row. */
struct BapServiceHandle final {
    std::uint32_t row{};
};

/** Pushes the query-only view of every proved global BAP service route. */
void push_bap_service_collection(lua_State* state);

/** Reads one member of that collection: a row, a lookup, or its count. */
[[nodiscard]] int bap_service_collection_index(lua_State* state);

/** Reads one member of a single service row. */
[[nodiscard]] int bap_service_index(lua_State* state);

/** Registers every locked BAP service userdata shape. */
void register_bap_metatables(lua_State* state);

} // namespace sunrise::server::activity::mission::lua_vm::detail
