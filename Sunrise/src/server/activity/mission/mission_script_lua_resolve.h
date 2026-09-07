#pragma once

#include <cstdint>
#include <string_view>

#include "mission_script_lua_types.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

// Every read through a handle re-resolves its row, so a handle kept across a generation change
// errors instead of addressing a reused row.

/** Rereads one Slot row so a stale generation fails instead of returning old data. */
[[nodiscard]] inline bool
current_slot(lua_State* state, const SlotHandle& handle, SlotDefinition& output) {
    Impl* const impl = impl_from_state(state);
    return impl != nullptr && impl->definitions.resolveSlotRow != nullptr
           && impl->definitions.resolveSlotRow(impl->definitions.context, handle.localRow, output);
}

/** Rereads one squad row so a stale generation fails instead of returning old data. */
[[nodiscard]] inline bool
current_squad(lua_State* state, const SquadHandle& handle, SquadDefinition& output) {
    Impl* const impl = impl_from_state(state);
    return impl != nullptr && impl->definitions.resolveSquadRow != nullptr
           && impl->definitions.resolveSquadRow(impl->definitions.context, handle.localRow, output);
}

[[nodiscard]] inline bool
current_scene(lua_State* state, const SceneHandle& handle, SceneDefinition& output) {
    Impl* const impl = impl_from_state(state);
    return impl != nullptr && impl->definitions.resolveSceneRow != nullptr
           && impl->definitions.resolveSceneRow(impl->definitions.context, handle.localRow, output);
}

/** Rereads one activity-message row so a stale generation fails instead of returning old data. */
[[nodiscard]] inline bool
resolve_message_row(lua_State* state, std::uint32_t localRow, ActivityMessageDefinition& output) {
    Impl* const impl = impl_from_state(state);
    return impl != nullptr && impl->definitions.resolveActivityMessageRow != nullptr
           && impl->definitions.resolveActivityMessageRow(
               impl->definitions.context, localRow, output);
}

[[nodiscard]] inline bool
resolve_message_id(lua_State* state, std::uint32_t messageId, ActivityMessageDefinition& output) {
    Impl* const impl = impl_from_state(state);
    return impl != nullptr && impl->definitions.resolveActivityMessageId != nullptr
           && impl->definitions.resolveActivityMessageId(
               impl->definitions.context, messageId, output);
}

[[nodiscard]] inline bool
current_message(lua_State* state, const MessageHandle& handle, ActivityMessageDefinition& output) {
    return resolve_message_row(state, handle.localRow, output);
}

[[nodiscard]] inline bool resolve_message_field_row(lua_State* state,
                                                    std::uint32_t messageRow,
                                                    std::uint32_t localRow,
                                                    ActivityMessageFieldDefinition& output) {
    Impl* const impl = impl_from_state(state);
    return impl != nullptr && impl->definitions.resolveActivityMessageFieldRow != nullptr
           && impl->definitions.resolveActivityMessageFieldRow(
               impl->definitions.context, messageRow, localRow, output);
}

[[nodiscard]] inline bool current_message_field(lua_State* state,
                                                const MessageFieldHandle& handle,
                                                ActivityMessageFieldDefinition& output) {
    return resolve_message_field_row(state, handle.messageRow, handle.localRow, output);
}

[[nodiscard]] inline bool current_activity_binding(lua_State* state,
                                                   ActivityBindingDefinition& output) noexcept {
    Impl* const impl = impl_from_state(state);
    return impl != nullptr && impl->definitions.resolveActivityBinding != nullptr
           && impl->definitions.resolveActivityBinding(impl->definitions.context, output);
}

[[nodiscard]] inline bool
current_activity_binding_locator(lua_State* state,
                                 const ActivityBindingLocatorHandle& handle,
                                 ActivityBindingLocatorDefinition& output) noexcept {
    Impl* const impl = impl_from_state(state);
    return impl != nullptr && impl->definitions.resolveActivityBindingLocator != nullptr
           && impl->definitions.resolveActivityBindingLocator(
               impl->definitions.context, handle.localRow, output);
}

// Selector resolvers, defined in mission_script_lua_context_api.cpp. Each takes a Lua stack index
// holding either a one-based row integer or an id string.

[[nodiscard]] bool resolve_squad(lua_State* state, int selector, SquadDefinition& output);
[[nodiscard]] bool resolve_scene(lua_State* state, int selector, SceneDefinition& output);
[[nodiscard]] bool resolve_slot(lua_State* state, int selector, SlotDefinition& output);
[[nodiscard]] bool
resolve_message_name(lua_State* state, std::string_view name, ActivityMessageDefinition& output);

} // namespace sunrise::server::activity::mission::lua_vm::detail
