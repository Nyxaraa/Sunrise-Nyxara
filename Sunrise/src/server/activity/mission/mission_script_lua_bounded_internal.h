#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "mission_script_lua_types.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

// Metatable names that lock the bounded-lane userdata shapes a program may hold.
inline constexpr char kBoundedLaneCollectionMetatable[] = "sunrise.sdk.bounded_lanes";
inline constexpr char kBoundedLaneMetatable[] = "sunrise.sdk.bounded_lane";
inline constexpr char kCountListMetatable[] = "sunrise.sdk.count_list";
inline constexpr char kObjectRefListMetatable[] = "sunrise.sdk.object_ref_list";
inline constexpr char kWorldPositionMetatable[] = "sunrise.sdk.world_position";

/** What a lane's elements are: plain counted values, or references to installed objects. */
enum class BoundedLaneKind : std::uint8_t {
    count,
    objectRef,
};

/** One fixed client array a body's count or index lane reaches. */
struct BoundedLane final {
    std::string_view name{};
    BoundedLaneKind kind{};
    std::uint16_t capacity{};
};

/**
 * Every lane whose array size is proved. The bound is the array the client indexes, never the
 * width of the wire lane; a lane with no proved array size is absent from this table.
 */
inline constexpr std::array<BoundedLane, 7> kBoundedLanes{{
    {"gameplay_switches", BoundedLaneKind::count, 50},
    {"nav_nodes", BoundedLaneKind::count, 96},
    {"nav_node_links", BoundedLaneKind::count, 16},
    {"type8_field_1_0", BoundedLaneKind::count, 32},
    {"type8_field_2_0", BoundedLaneKind::count, 16},
    {"type8_object_refs", BoundedLaneKind::objectRef, 16},
    {"type53_object_refs", BoundedLaneKind::objectRef, 129},
}};

/** Storage for the widest count lane and the widest object-reference lane. */
inline constexpr std::size_t kCountListCapacity = 96;
inline constexpr std::size_t kObjectRefListCapacity = 129;

struct BoundedLaneCollectionHandle final {
    std::uint8_t marker{};
};

struct BoundedLaneHandle final {
    std::uint8_t row{};
};

/** A list whose length can never exceed its lane's array, so the count is never a parameter. */
struct CountListHandle final {
    std::array<std::int32_t, kCountListCapacity> values{};
    std::uint8_t lane{};
    std::uint8_t count{};
};

/** A list of installed object rows. Every element came from a resolved SlotView. */
struct ObjectRefListHandle final {
    std::array<std::uint32_t, kObjectRefListCapacity> slotRows{};
    std::uint8_t lane{};
    std::uint8_t count{};
};

/** Three finite world coordinates. The magnitude bound is unproved, so none is applied. */
struct WorldPositionHandle final {
    std::array<float, 3> value{};
};

void push_bounded_lane(lua_State* state, std::uint8_t row);
void push_world_position(lua_State* state, const std::array<float, 3>& value);

/** Registers every locked bounded-lane userdata shape. */
void register_bounded_metatables(lua_State* state);

/** Pushes one recognized ActivityView bounded member and returns whether the key was owned. */
[[nodiscard]] bool push_bounded_activity_member(lua_State* state, std::string_view key);

} // namespace sunrise::server::activity::mission::lua_vm::detail
