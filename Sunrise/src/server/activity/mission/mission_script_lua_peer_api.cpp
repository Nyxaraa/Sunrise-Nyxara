// Peer reads. context.peers mints a locked view over the peer set the host last published, and
// every member read re-resolves its row, so a held view never answers for another peer.

#include <cstdint>
#include <string_view>

#include "mission_script_lua_internal.h"
#include "mission_script_lua_peer_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

namespace {

/** Rereads one row of the published peer set. A row the set no longer holds faults the callback. */
[[nodiscard]] const PeerSession& current_peer(lua_State* state, std::uint8_t row) {
    const Impl* const impl = impl_from_state(state);
    if (row >= impl->peerCount) {
        raise_lua_error(state, "peer row is outside the published set");
    }
    return impl->peers[row];
}

/** Mints the view of one peer the published set still holds. */
[[nodiscard]] int peer_collection_at(lua_State* state) {
    static_cast<void>(luaL_checkudata(state, 1, kPeerCollectionMetatable));
    const Impl* const impl = impl_from_state(state);
    const lua_Integer row = luaL_checkinteger(state, 2);
    if (row < 1 || static_cast<std::uint64_t>(row) > impl->peerCount) {
        return luaL_error(state, "peer row is outside the published set");
    }
    push_peer_view(state, static_cast<std::uint8_t>(row - 1));
    return 1;
}

/** Reads count, or the accessor for one peer. */
[[nodiscard]] int peer_collection_index(lua_State* state) {
    static_cast<void>(luaL_checkudata(state, 1, kPeerCollectionMetatable));
    const std::string_view key = lua_string_view(state, 2);
    if (key == "count") {
        const Impl* const impl = impl_from_state(state);
        lua_pushinteger(state, impl->peerCount);
    } else if (key == "at") {
        lua_pushcfunction(state, &peer_collection_at);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

/** Reads one member of a peer session. All four are 64-bit, so they cross as decimal strings. */
[[nodiscard]] int peer_view_index(lua_State* state) {
    const auto* const handle =
        static_cast<const PeerViewHandle*>(luaL_checkudata(state, 1, kPeerViewMetatable));
    const PeerSession& peer = current_peer(state, handle->row);
    const std::string_view key = lua_string_view(state, 2);
    if (key == "session_id") {
        push_u64_string(state, peer.sessionId);
    } else if (key == "session_generation") {
        push_u64_string(state, peer.sessionGeneration);
    } else if (key == "member_key") {
        push_u64_string(state, peer.memberKey);
    } else if (key == "join_identity") {
        push_u64_string(state, peer.joinIdentity);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

} // namespace

void push_peers(lua_State* state) {
    push_handle(state, kPeerCollectionMetatable, PeerCollectionHandle{});
}

void push_peer_view(lua_State* state, std::uint8_t row) {
    push_handle(state, kPeerViewMetatable, PeerViewHandle{row});
}

void register_peer_metatables(lua_State* state) {
    register_metatable(state, kPeerCollectionMetatable, &peer_collection_index);
    register_metatable(state, kPeerViewMetatable, &peer_view_index);
}

} // namespace sunrise::server::activity::mission::lua_vm::detail
