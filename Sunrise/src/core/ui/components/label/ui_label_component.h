#pragma once

#include <imgui.h>

#include "../../scaling/dpi/ui_dpi_scaling.h"

namespace sunrise::core::ui::components::label {

/** 10 authored pixels inset a label from the left edge of its row. */
constexpr float kInset = 10.0F;

/** @return The shared label inset, scaled for the display. */
[[nodiscard]] inline float inset() noexcept {
    return scaling::dpi::pixels(kInset);
}

/** Moves the cursor so the next item starts at the shared label inset. */
inline void align() noexcept {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + inset());
}

} // namespace sunrise::core::ui::components::label
