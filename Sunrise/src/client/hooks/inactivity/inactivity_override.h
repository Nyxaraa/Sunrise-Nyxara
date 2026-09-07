#pragma once

namespace sunrise::client::hooks::inactivity {

/**
 * Resolves the activity config getter, which the timeouts are reached through.
 * @return True when it was found.
 */
[[nodiscard]] bool install() noexcept;

/** Puts the Client's own timeouts back and drops the resolved getter. */
void uninstall() noexcept;

/**
 * Holds every timeout at its longest while the setting is on, or puts the Client's own back.
 * Enters Client code, so call it once a frame from a tick that holds no lock.
 */
void poll() noexcept;

} // namespace sunrise::client::hooks::inactivity
