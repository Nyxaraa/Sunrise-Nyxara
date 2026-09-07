#pragma once

#include <cstdint>

namespace sunrise::client::hooks::membership_probe {

/**
 * Attaches the read-only probe on the client's activity msg 12 handler.
 * The detour changes nothing: it logs and calls through.
 * @return True when the probe is attached, or was already.
 */
[[nodiscard]] bool install() noexcept;

/**
 * Samples every ActivityClient the probe has seen recently.
 * Sampling stops a bounded time after a client's last message, so no pointer is read once stale.
 * @param now Monotonic tick count in milliseconds.
 */
void service(std::uint64_t now) noexcept;

/** Detaches the probe so a later unload cannot leave a detour into unmapped code. */
[[nodiscard]] bool uninstall() noexcept;

} // namespace sunrise::client::hooks::membership_probe
