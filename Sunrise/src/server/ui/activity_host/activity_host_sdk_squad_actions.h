#pragma once

#include <cstdint>
#include <imgui.h>

#include "../../../state/activity_sdk/runtime.h"

namespace sunrise::server::ui::activity_host::sdk_squad_view {

/** One shared table style, so every table on this page reads the same. */
inline constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                               | ImGuiTableFlags_ScrollY
                                               | ImGuiTableFlags_SizingFixedFit;
/** The page style plus horizontal scroll and resize, for a table wider than the pane. */
inline constexpr ImGuiTableFlags kWideTableFlags =
    kTableFlags | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable;

/** Forgets which squad the inputs were built for, and its last place result. */
void reset_squad_action_inputs() noexcept;

/**
 * Initializes member requests from the exact positive generated defaults.
 * @param catalog Bound generated catalog.
 * @param squad Selected squad row.
 * @param squadRow Its global row, which the inputs are kept for.
 */
void initialize_inputs(const state::activity_sdk::Catalog& catalog,
                       const state::activity_sdk::format::Squad& squad,
                       std::uint32_t squadRow) noexcept;

/** Draws exact authored anchors without assigning gameplay semantics to their positions. */
void draw_anchors(const state::activity_sdk::Catalog& catalog,
                  const state::activity_sdk::format::Squad& squad) noexcept;

/**
 * Draws every bounded option carried by the native type-1 encoder.
 * @param view Resolved bound view the action is sent for.
 * @param squad Selected squad row.
 * @param squadRow Its global row, which the wire action names.
 */
void draw_place_action(const state::activity_sdk::BoundView& view,
                       const state::activity_sdk::format::Squad& squad,
                       std::uint32_t squadRow) noexcept;

/** Offers every exact type-43 scene which authors behavior for the selected type-1 squad. */
void draw_authored_behavior_scenes(const state::activity_sdk::BoundView& view,
                                   const state::activity_sdk::format::Squad& squad) noexcept;

} // namespace sunrise::server::ui::activity_host::sdk_squad_view
