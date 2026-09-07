#pragma once

#include "../../../client/ui/activity/authored_placement_marker.h"

namespace sunrise::server::ui::activity_host::anchor_render_controls {

/** Draws shared package-marker presentation controls and commits bounded options. */
void draw_options(client::ui::activity::authored_placement_marker::State& state) noexcept;

/** Draws shared marker selection and render-cap diagnostics for one exact context. */
void draw_status(const client::ui::activity::authored_placement_marker::Context& context,
                 const client::ui::activity::authored_placement_marker::State& state) noexcept;

} // namespace sunrise::server::ui::activity_host::anchor_render_controls
