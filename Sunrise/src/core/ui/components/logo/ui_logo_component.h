#pragma once

namespace sunrise::core::ui::components::logo {

/**
 * Draws the current frame of the animated Sunrise logo at the cursor. The art is grayscale, so
 * it is tinted with the theme accent.
 * @param extent Final framebuffer size of the square logo.
 * @return True when the sheet is uploaded and a frame was drawn. Nothing is drawn otherwise.
 */
[[nodiscard]] bool draw(float extent) noexcept;

} // namespace sunrise::core::ui::components::logo
