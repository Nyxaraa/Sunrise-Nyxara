#pragma once

namespace sunrise::client::hooks::sense_chain_guard {

/** Attaches the sense-record chain guard. @return True when the detour attaches. */
[[nodiscard]] bool install() noexcept;

/** Detaches the sense-record chain guard. */
[[nodiscard]] bool uninstall() noexcept;

} // namespace sunrise::client::hooks::sense_chain_guard
