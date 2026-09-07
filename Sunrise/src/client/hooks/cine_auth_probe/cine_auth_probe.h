#pragma once

namespace sunrise::client::hooks::cine_auth_probe {

/**
 * Attaches the read-only probe on the type-6 cinematic Auth apply and start chain.
 * Its start gates refuse without any event, so each detour logs the compared values and the
 * outcome at debug level, one line per transition.
 * @return True when every target is found and all detours attach, or they already did.
 */
[[nodiscard]] bool install() noexcept;

/** Detaches the probe so a later unload cannot leave a detour into unmapped code. */
[[nodiscard]] bool uninstall() noexcept;

} // namespace sunrise::client::hooks::cine_auth_probe
