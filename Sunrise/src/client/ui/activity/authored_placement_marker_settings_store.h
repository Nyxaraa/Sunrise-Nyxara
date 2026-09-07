#pragma once

#include "authored_placement_marker.h"

namespace sunrise::client::ui::activity::authored_placement_marker::settings_store {

/** Resolves and loads the module-owned world-marker presentation file. */
void initialize(void* module) noexcept;

/** Drops the loaded presentation and resolved file path. */
void shutdown() noexcept;

/** @return One lock-consistent copy of the loaded marker presentation. */
[[nodiscard]] Options get() noexcept;

/** Publishes one valid marker presentation and writes it straight to disk. */
[[nodiscard]] bool publish(const Options& options) noexcept;

} // namespace sunrise::client::ui::activity::authored_placement_marker::settings_store
