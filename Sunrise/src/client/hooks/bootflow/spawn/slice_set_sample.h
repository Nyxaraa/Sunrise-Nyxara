#pragma once

#include <cstdint>

namespace sunrise::client::hooks::bootflow::spawn {

/**
 * Finds the spawn gate and the four calls it reads the current slice set through.
 * @return True when every call decoded.
 */
[[nodiscard]] bool install_targets() noexcept;

/** Clears the calls it found. */
void uninstall_targets() noexcept;

/** @return The addressable local slice-set index, -1 while absent, or -2 when unresolved. */
[[nodiscard]] std::int32_t sample_current_slice_set() noexcept;

} // namespace sunrise::client::hooks::bootflow::spawn
