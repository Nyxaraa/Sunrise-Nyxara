#pragma once

#include "../../../state/activity_sdk/runtime.h"

namespace sunrise::server::ui::activity_host::sdk_mission_view {

/** Draws the automatically selected state-0 roster plan. */
void draw(const state::activity_sdk::BoundView& view,
          const state::activity_sdk::format::Scenario& scenario) noexcept;

/** Draws searchable type-43 authored scenes and their one-generation activate action. */
void draw_scenes(const state::activity_sdk::BoundView& view) noexcept;

/** Draws the type-53 authored dialogue cues and their play action. */
void draw_dialogue(const state::activity_sdk::BoundView& view) noexcept;

/** Draws the type-68 HUD directives and their enter, complete, exit and clear actions. */
void draw_directives(const state::activity_sdk::BoundView& view) noexcept;

/** Draws the type-3 objectives and type-38 tasks with their reset and advance actions. */
void draw_objectives(const state::activity_sdk::BoundView& view) noexcept;

/** Draws the type-5 sequences and type-6 cinematics with their play and stop actions. */
void draw_cinematics(const state::activity_sdk::BoundView& view) noexcept;

/** Draws the compiled object behavior roots, which carry no action. */
void draw_compiled_behaviors(const state::activity_sdk::BoundView& view) noexcept;

} // namespace sunrise::server::ui::activity_host::sdk_mission_view
