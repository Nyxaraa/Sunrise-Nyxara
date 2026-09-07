#pragma once

namespace sunrise::server::ui::activity_host {

/** Draws the Activity Host diagnostics and typed state controls. */
void draw() noexcept;

/** Draws enabled companion windows while the main UI is visible. */
void draw_windows() noexcept;

} // namespace sunrise::server::ui::activity_host
