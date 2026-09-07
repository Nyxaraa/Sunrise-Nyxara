#pragma once

#include "../../../state/activity_sdk/runtime.h"

namespace sunrise::server::ui::activity_host::sdk_squad_view {

/** Draws generated scenario squads and the guarded server-side place action. */
void draw(const state::activity_sdk::BoundView& view,
          const state::activity_sdk::format::Scenario& scenario) noexcept;

} // namespace sunrise::server::ui::activity_host::sdk_squad_view
