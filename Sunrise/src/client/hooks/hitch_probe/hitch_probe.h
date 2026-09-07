#pragma once

namespace sunrise::client::hooks::hitch_probe {

/** Attaches the read-only hitch snapshot dump. @return True when the detour attaches. */
[[nodiscard]] bool install() noexcept;

/** Detaches the hitch snapshot dump. */
[[nodiscard]] bool uninstall() noexcept;

} // namespace sunrise::client::hooks::hitch_probe
