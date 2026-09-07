#pragma once

#include <string_view>

#include "mission_script_vm_internal.h"

// Typed event views. One metatable per EventKind, each exposing only the members its producer
// writes. host::Event is one value type with four unions, so a member read on a kind that did not
// write that arm returns another field's bytes.

namespace sunrise::server::activity::mission::lua_vm::detail {

/** Prefix every event view metatable name carries. check_event tests for it. */
inline constexpr std::string_view kEventMetatablePrefix = "sunrise.mission.event.";

/**
 * Reads one event argument of any view.
 * @param index Stack index of the argument.
 * @return The event, after raising a Lua error when the argument is not an event view.
 */
[[nodiscard]] const host::Event& check_event(lua_State* state, int index);

/** @return The catalog Lua name of one event kind, or an empty view when it is hidden. */
[[nodiscard]] std::string_view event_kind_name(lua_State* state, host::EventKind kind) noexcept;

/** @return False when the catalog publishes no surface for this kind, hiding its own members. */
[[nodiscard]] bool event_surface_visible(lua_State* state, host::EventKind kind) noexcept;

/** @return The view metatable one kind is pushed with. */
[[nodiscard]] const char* event_metatable(host::EventKind kind) noexcept;

// Members shared by more than one family. Each pushes the member named by key and returns true
// when it owns that key. A view calls the helpers for the members its producer writes, and no
// others. Members used by one family only live in that family's translation unit.

/** kind, sequence and source_generation. Every view carries these three. */
[[nodiscard]] bool
push_common_member(lua_State* state, const host::Event& event, std::string_view key);

/** mission_sequence, the per-binding input order. Union 2's mission arm. */
[[nodiscard]] bool
push_mission_sequence_member(lua_State* state, const host::Event& event, std::string_view key);

/** state_revision, union 1's state arm. */
[[nodiscard]] bool
push_state_revision_member(lua_State* state, const host::Event& event, std::string_view key);

/** registry_key, object_tag, slot_index, slot_type and the slot handle they resolve to. */
[[nodiscard]] bool
push_slot_identity_member(lua_State* state, const host::Event& event, std::string_view key);

/** The four bounded outer counts of one msg-19 body. Carries no selector or payload bytes. */
[[nodiscard]] bool
push_incident_body_member(lua_State* state, const host::Event& event, std::string_view key);

// Metatable registration, one function per family.

void register_input_event_metatables(lua_State* state);
void register_delivery_event_metatables(lua_State* state);
void register_derived_event_metatables(lua_State* state);

} // namespace sunrise::server::activity::mission::lua_vm::detail
