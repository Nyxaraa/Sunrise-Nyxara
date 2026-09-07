#pragma once

namespace sunrise::client::hooks::queuez {

/** Attaches the queuez null-payload guard. @return True when every fix attached. */
[[nodiscard]] bool install() noexcept;

/** Detaches the queuez null-payload guard. */
void uninstall() noexcept;

/** @return True while at least one fix is attached. */
[[nodiscard]] bool is_installed() noexcept;

} // namespace sunrise::client::hooks::queuez
