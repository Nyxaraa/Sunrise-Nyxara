#pragma once

#include "activity_host_authored_anchors.h"

namespace sunrise::server::activity::host {
struct InstanceSnapshot;
}

namespace sunrise::state::build_data::scriptables {
struct Snapshot;
}

namespace sunrise::server::ui::activity_host::package_aabbs {

/**
 * Browses package AABBs without submitting unassociated geometry to the renderer.
 * TODO: no page calls this. It waits on the exact AABB-to-ClientRef edge; a bulk AABB render
 * overruns the diagnostic pass, so the marker backend refuses every AABB anchor today.
 */
void draw(const state::build_data::scriptables::Snapshot& snapshot,
          const server::activity::host::InstanceSnapshot& instance,
          const authored_anchors::Filters& filters) noexcept;

} // namespace sunrise::server::ui::activity_host::package_aabbs
