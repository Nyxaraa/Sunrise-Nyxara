#pragma once

namespace sunrise::client::hooks::cine_probe {

/**
 * Attaches the read-only cinematic entry probe.
 * It logs once per second which readiness stage refused, plus a freeze-discriminator line for
 * the mode machine: tick count, both advance-gate arms, clock vs duration, and heal counters.
 * @return True when every target is found and all detours attach, or they already did.
 */
[[nodiscard]] bool install() noexcept;

/** Detaches the probe so a later unload cannot leave a detour into unmapped code. */
[[nodiscard]] bool uninstall() noexcept;

} // namespace sunrise::client::hooks::cine_probe
