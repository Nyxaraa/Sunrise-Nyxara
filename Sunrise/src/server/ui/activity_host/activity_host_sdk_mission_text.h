#pragma once

#include <imgui.h>
#include <string_view>

#include "../../../state/activity_sdk/runtime.h"

namespace sunrise::server::ui::activity_host::sdk_mission_view {

/** One shared table style, so every table on this page reads the same. */
inline constexpr ImGuiTableFlags kSceneTableFlags =
    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX
    | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

/** @return A generated string or a stable missing marker. */
[[nodiscard]] std::string_view display_text(const state::activity_sdk::Catalog& catalog,
                                            state::activity_sdk::format::StringRef value) noexcept;

/** @return A string length bounded for printf-style rendering. */
[[nodiscard]] int print_length(std::string_view value) noexcept;

/** ASCII case-insensitive substring match for stable generated IDs and numeric search text. */
[[nodiscard]] bool contains_folded(std::string_view value, std::string_view query) noexcept;

/** @return Text of the one search box every page here shares, without its zero storage. */
[[nodiscard]] std::string_view search_text() noexcept;

} // namespace sunrise::server::ui::activity_host::sdk_mission_view
