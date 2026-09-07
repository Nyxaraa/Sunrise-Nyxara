#pragma once

#include <span>

#include "../../../state/activity_sdk/runtime.h"

namespace sunrise::server::ui::activity_host::sdk_view {

/**
 * Keeps object and slot selection pinned to one catalog and client generation.
 * @param view Resolved bound view the panel is drawing.
 * @param occurrences Scenario occurrences the selection indexes into.
 */
void sync_selection(const state::activity_sdk::BoundView& view,
                    std::span<const state::activity_sdk::format::Occurrence> occurrences) noexcept;

} // namespace sunrise::server::ui::activity_host::sdk_view
