#pragma once

#include <cstdint>

#include "activity_host_authored_anchors.h"

namespace sunrise::server::activity::host {
struct InstanceSnapshot;
}

namespace sunrise::state::build_data::scriptables {
struct Snapshot;
}

namespace sunrise::server::ui::activity_host::trigger_volumes {

/** Draws the exact slot-owned package trigger-volume browser and selected-only controls. */
void draw(const state::build_data::scriptables::Snapshot& snapshot,
          const server::activity::host::InstanceSnapshot& instance,
          const authored_anchors::Filters& filters) noexcept;

/** Draws shapes linked to one selected slot. */
void draw_selected(const state::build_data::scriptables::Snapshot& snapshot,
                   const server::activity::host::InstanceSnapshot& instance,
                   std::uint32_t slotRow) noexcept;

} // namespace sunrise::server::ui::activity_host::trigger_volumes
