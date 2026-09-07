#pragma once

namespace sunrise::client::hooks::stall_probe {

/**
 * Starts the stall watcher thread.
 * It polls the pump call count; when the game stops calling the pump it captures every thread's
 * rip and in-image return addresses, which names the blocked code.
 */
bool install() noexcept;

/** Stops the watcher thread. */
bool uninstall() noexcept;

} // namespace sunrise::client::hooks::stall_probe
