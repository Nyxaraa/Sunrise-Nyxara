#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "../../../middleware/bap/activity_message/scriptable_auth_body.h"
#include "../host_runtime.h"
#include "mission_script_lua_types.h"
#include "mission_script_lua_value_internal.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

// Metatable names that lock the device and lifetime enum userdata shapes a program may hold.
inline constexpr char kDeviceChannelMetatable[] = "sunrise.sdk.device_channel";
inline constexpr char kDeviceChannelCollectionMetatable[] = "sunrise.sdk.device_channels";
inline constexpr char kDeviceTransitionMetatable[] = "sunrise.sdk.device_transition";
inline constexpr char kDeviceTransitionCollectionMetatable[] = "sunrise.sdk.device_transitions";
inline constexpr char kLifetimeStateMetatable[] = "sunrise.sdk.lifetime_state";
inline constexpr char kLifetimeStateCollectionMetatable[] = "sunrise.sdk.lifetime_states";

namespace scriptable_auth = middleware::bap::activity_message::scriptable_auth;

/** One named transition over the position, power, or lock lane. */
struct DeviceTransition final {
    std::string_view name{};
    scriptable_auth::Type23Channel channel{};
    float value{};
};

/** The closed transition vocabulary. Each word names one lane and one of its two endpoints. */
inline constexpr std::array<DeviceTransition, 6> kDeviceTransitions{{
    {"open", scriptable_auth::Type23Channel::devicePosition, kUnitLaneHigh},
    {"close", scriptable_auth::Type23Channel::devicePosition, kUnitLaneLow},
    {"power_on", scriptable_auth::Type23Channel::devicePower, kUnitLaneHigh},
    {"power_off", scriptable_auth::Type23Channel::devicePower, kUnitLaneLow},
    {"lock", scriptable_auth::Type23Channel::deviceLock, kUnitLaneHigh},
    {"unlock", scriptable_auth::Type23Channel::deviceLock, kUnitLaneLow},
}};

/** Lifetime states 11 to 14 index past the client jump table, so only these have a spelling. */
inline constexpr std::uint8_t kLifetimeStateCount = host::kMaximumLifetimeState + 1U;

struct DeviceChannelHandle final {
    std::uint8_t channel{};
};

struct DeviceChannelCollectionHandle final {
    std::uint8_t marker{};
};

/** One row of the closed transition vocabulary. */
struct DeviceTransitionHandle final {
    std::uint8_t row{};
};

struct DeviceTransitionCollectionHandle final {
    std::uint8_t marker{};
};

struct LifetimeStateHandle final {
    std::uint8_t state{};
};

struct LifetimeStateCollectionHandle final {
    std::uint8_t marker{};
};

/** @return The Lua spelling of one device channel, or "unknown". */
[[nodiscard]] std::string_view device_channel_name(std::uint8_t channel) noexcept;

/** @return True when the word names a device channel; output is the typed wire lane. */
[[nodiscard]] bool device_channel_value(std::string_view name, std::uint8_t& output) noexcept;

void push_device_channel(lua_State* state, std::uint8_t channel);
void push_device_transition(lua_State* state, std::uint8_t row);
void push_lifetime_state(lua_State* state, std::uint8_t value);

/** Registers every locked enum userdata shape. */
void register_enum_metatables(lua_State* state);

/** Pushes one recognized ActivityView enum member and returns whether the key was owned. */
[[nodiscard]] bool push_enum_activity_member(lua_State* state, std::string_view key);

} // namespace sunrise::server::activity::mission::lua_vm::detail
