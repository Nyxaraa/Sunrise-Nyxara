#pragma once

namespace sunrise::client::hooks::net_tick_probe {

/**
 * Reports the game's networking-tick latch and our own callback count.
 * Read-only, self-throttled, and safe on any thread. Call it from the pump for the healthy
 * cadence and from the assert observer, which is the only thing still running after a stall.
 */
void sample() noexcept;

} // namespace sunrise::client::hooks::net_tick_probe
