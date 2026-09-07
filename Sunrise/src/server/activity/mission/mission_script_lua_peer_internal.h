#pragma once

#include <cstdint>

#include "mission_script_lua_types.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

// One locked view per peer read. A handle addresses a row of the peer set the host last published
// and copies nothing, so every member read re-resolves that row before it answers.
inline constexpr char kPeerCollectionMetatable[] = "sunrise.mission.peers";
inline constexpr char kPeerViewMetatable[] = "sunrise.mission.peer_view";

/** The peer set the host last published. It addresses no row, so it carries a presence marker. */
struct PeerCollectionHandle final {
    std::uint8_t marker{};
};

/** One peer session, at its zero-based row inside the published set. */
struct PeerViewHandle final {
    std::uint8_t row{};
};

void push_peers(lua_State* state);
void push_peer_view(lua_State* state, std::uint8_t row);

/** Registers every locked peer userdata shape. */
void register_peer_metatables(lua_State* state);

} // namespace sunrise::server::activity::mission::lua_vm::detail
