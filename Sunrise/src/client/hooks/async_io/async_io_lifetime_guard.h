#pragma once

namespace sunrise::client::hooks::async_io {

/** Installs the native async-manager cached-owner guard when this game build matches. */
[[nodiscard]] bool install() noexcept;

/** Restores the exact original instruction bytes when this module installed the guard. */
[[nodiscard]] bool uninstall() noexcept;

/** @return True while the guarded instruction sequence is present. */
[[nodiscard]] bool is_installed() noexcept;

} // namespace sunrise::client::hooks::async_io
