#pragma once

#include <cstdint>

#include "../../../state/activity_sdk/runtime.h"
#include "../../activity/activity_sdk_mission_runtime.h"

namespace sunrise::server::ui::activity_host::sdk_mission_view {

/** The two action families the behavior inventory draws, one page each. */
enum class BehaviorFamily : std::uint8_t {
    objectives,
    cinematics,
};

/**
 * Lists every state-local authored behavior row, including explicitly unsupported surfaces.
 * @param view Resolved bound view the page is drawing.
 * @param snapshot Mission state already queried for this frame.
 * @param family Page whose slot types are listed.
 */
void draw_behavior_inventory(const state::activity_sdk::BoundView& view,
                             const server::activity::activity_sdk_mission::Snapshot& snapshot,
                             BehaviorFamily family) noexcept;

/** Lists every installed compiled root. A root tag is not a callable wire action. */
void draw_compiled_behavior_roots(const state::activity_sdk::Catalog& catalog) noexcept;

/** Forgets the last objective, task and cinematic action result. */
void reset_behavior_action_status() noexcept;

} // namespace sunrise::server::ui::activity_host::sdk_mission_view
