#include "ui_hud_logo_overlay.h"

#include <algorithm>
#include <imgui.h>

#include "../../../../../resources/resource.h"
#include "../../components/logo/ui_logo_component.h"
#include "../../scaling/dpi/ui_dpi_scaling.h"

namespace sunrise::core::ui::hud::overlays::logo {
namespace {

/** 44 authored pixels make the logo as tall as the two text rows beside it. */
constexpr float kLogoExtent = 44.0F;
/** The card carries the name and the version, one row each. */
constexpr float kTextRowCount = 2.0F;
/** Half a difference centers the shorter column against the taller one. */
constexpr float kHalfExtent = 2.0F;
/** The card names the tool with the same wordmark the main surface carries. */
constexpr char kTitle[] = "SUNRISE";

} // namespace

/** Draws the Sunrise card inside the overlay window the stack has already started. */
void draw() noexcept {
    const float extent = scaling::dpi::pixels(kLogoExtent);
    if (components::logo::draw(extent)) {
        ImGui::SameLine();
        // The text column is shorter than the logo, so it starts lower to sit level with it.
        const float textHeight =
            (ImGui::GetTextLineHeight() * kTextRowCount) + ImGui::GetStyle().ItemSpacing.y;
        const float offset = (std::max)(extent - textHeight, 0.0F) / kHalfExtent;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
    }
    ImGui::BeginGroup();
    ImGui::TextUnformatted(kTitle);
    ImGui::TextDisabled("%s", SUNRISE_VER_STRING);
    ImGui::EndGroup();
}

} // namespace sunrise::core::ui::hud::overlays::logo
