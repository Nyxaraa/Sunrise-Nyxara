#pragma once

namespace sunrise::client::hooks::retail_log {

/** Installs the game retail-log capture hook when its targets and settings allow it. */
[[nodiscard]] bool install() noexcept;

/** Removes the retail-log capture hook. */
[[nodiscard]] bool uninstall() noexcept;

} // namespace sunrise::client::hooks::retail_log
