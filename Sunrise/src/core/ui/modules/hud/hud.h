#pragma once

namespace sunrise::core::ui::modules::hud {

/**
 * Loads the saved overlay switches, then registers the page.
 * @param module Loaded DLL used to resolve the owned artifact directory.
 * @return True when the Core HUD page owns its registry slot.
 */
[[nodiscard]] bool initialize(void* module) noexcept;

/** Removes the Core HUD page and drops the switch file path. */
void shutdown() noexcept;

} // namespace sunrise::core::ui::modules::hud
