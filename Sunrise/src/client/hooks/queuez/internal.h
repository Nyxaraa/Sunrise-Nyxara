#pragma once

namespace sunrise::client::hooks::queuez {

/**
 * Attaches the update-notification null-payload guard.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install_null_payload_guard() noexcept;

/** Detaches the null-payload guard. */
void uninstall_null_payload_guard() noexcept;

/** @return True while the null-payload guard is attached. */
[[nodiscard]] bool null_payload_guard_installed() noexcept;

} // namespace sunrise::client::hooks::queuez
