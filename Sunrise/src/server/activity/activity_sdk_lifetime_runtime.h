#pragma once

#include <cstdint>

#include "../../state/activity_sdk/runtime.h"
#include "activity_sdk_device_runtime.h"

namespace sunrise::server::activity::host {
struct ScriptableOutputReservation;
}

namespace sunrise::server::activity::activity_sdk_lifetime {

/** Shared SDK adapter refusal surface; `invalidValue` is an out-of-range lifetime state. */
using Status = activity_sdk_devices::Status;

/** Checks one activity lifetime change without changing Activity Host output state. */
[[nodiscard]] Status availability(const state::activity_sdk::BoundView& view,
                                  std::uint8_t lifetimeState) noexcept;

/** Sets one activity lifetime state for an operator action. */
[[nodiscard]] Status set(const state::activity_sdk::BoundView& view,
                         std::uint8_t lifetimeState) noexcept;

/** Queues one lifetime change only through the exact unarmed Host revision owned by Mission. */
[[nodiscard]] Status set_reserved(const state::activity_sdk::BoundView& view,
                                  std::uint8_t lifetimeState,
                                  const host::ScriptableOutputReservation& reservation) noexcept;

} // namespace sunrise::server::activity::activity_sdk_lifetime
