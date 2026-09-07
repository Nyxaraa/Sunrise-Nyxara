#pragma once

#include <algorithm>
#include <imgui.h>

#include "../../../core/ui/scaling/dpi/ui_dpi_scaling.h"

namespace sunrise::server::ui::activity_host::tool_window {

/** The transparent parent moves and resizes while the card owns content scrolling. */
constexpr ImGuiWindowFlags kWindowFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
    | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

/** Applies a bounded content size and a first-use position. */
inline void set_next(const ImVec2& authoredPosition, const ImVec2& authoredSize) noexcept {
    namespace scaling = core::ui::scaling::dpi;
    ImVec2 size = scaling::pixels(authoredSize);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
        return;
    }
    const float margin = scaling::pixels(16.0F);
    const ImVec2 maximum{(std::max)(viewport->WorkSize.x - (margin * 2.0F), 1.0F),
                         (std::max)(viewport->WorkSize.y - (margin * 2.0F), 1.0F)};
    size.x = (std::min)(size.x, maximum.x);
    size.y = (std::min)(size.y, maximum.y);
    const ImVec2 requestedMinimum = scaling::pixels({320.0F, 120.0F});
    const ImVec2 minimum{(std::min)(requestedMinimum.x, maximum.x),
                         (std::min)(requestedMinimum.y, maximum.y)};
    ImGui::SetNextWindowSizeConstraints(minimum, maximum);
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);

    const ImVec2 requested = scaling::pixels(authoredPosition);
    const ImVec2 available{(std::max)(viewport->WorkSize.x - size.x, 0.0F),
                           (std::max)(viewport->WorkSize.y - size.y, 0.0F)};
    const ImVec2 position{viewport->WorkPos.x + std::clamp(requested.x, 0.0F, available.x),
                          viewport->WorkPos.y + std::clamp(requested.y, 0.0F, available.y)};
    ImGui::SetNextWindowPos(position, ImGuiCond_FirstUseEver);
}

/** Begins a transparent movable parent whose entire visible surface is the child card. */
[[nodiscard]] inline bool begin(const char* id,
                                bool& open,
                                const ImVec2& authoredPosition,
                                const ImVec2& authoredSize) noexcept {
    set_next(authoredPosition, authoredSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
    const bool visible = ImGui::Begin(id, &open, kWindowFlags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return visible;
}

} // namespace sunrise::server::ui::activity_host::tool_window
