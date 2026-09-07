#pragma once

#include <algorithm>
#include <cstddef>
#include <imgui.h>

#include "../../../core/ui/scaling/dpi/ui_dpi_scaling.h"

namespace sunrise::server::ui::activity_host::table_layout {

/** Eight rows leave room for the selected-object workspace below each browser. */
inline constexpr std::size_t kVisibleRowLimit = 8;

/** @return The minimum height used by each data row. */
[[nodiscard]] inline float row_height() noexcept {
    return ImGui::GetTextLineHeightWithSpacing() * 1.25F;
}

/** @return A table size that fits present rows up to a scrolling cap. */
[[nodiscard]] inline ImVec2 size(std::size_t rows,
                                 std::size_t maximumVisibleRows = kVisibleRowLimit) noexcept {
    const std::size_t visible = (std::min)(rows, (std::min)(maximumVisibleRows, kVisibleRowLimit));
    const float header = ImGui::GetTextLineHeightWithSpacing();
    const float border = ImGui::GetStyle().CellPadding.y * 2.0F + 2.0F;
    return {0.0F, header + (row_height() * static_cast<float>(visible)) + border};
}

/** @return Authored window height occupied by a bounded set of data rows. */
[[nodiscard]] inline float
authored_rows_height(std::size_t rows, std::size_t maximumVisibleRows = kVisibleRowLimit) noexcept {
    const float scale = (std::max)(core::ui::scaling::dpi::current(), 0.01F);
    const std::size_t visible = (std::min)(rows, (std::min)(maximumVisibleRows, kVisibleRowLimit));
    return row_height() * static_cast<float>(visible) / scale;
}

/** Freezes the header row before emitting it. */
inline void frozen_headers() noexcept {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
}

/** Starts one data row at the selected shared height. */
inline void next_row() noexcept {
    ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height());
}

/** Draws a full-row selector whose selected hover state stays distinct. */
[[nodiscard]] inline bool selectable(const char* label, bool selected) noexcept {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                              ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    }
    const bool pressed = ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns);
    if (selected) {
        ImGui::PopStyleColor();
    }
    return pressed;
}

} // namespace sunrise::server::ui::activity_host::table_layout
