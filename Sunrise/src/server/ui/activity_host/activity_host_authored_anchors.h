#pragma once

struct ImGuiTextFilter;

namespace sunrise::server::activity::host {
struct InstanceSnapshot;
}

namespace sunrise::state::build_data::scriptables {
struct Snapshot;
}

namespace sunrise::server::ui::activity_host::authored_anchors {

/** Shared browser filters applied to the independently selected authored-anchor rows. */
struct Filters final {
    const ImGuiTextFilter* text{};
    int bubbleIndex{-1};
    int stateIndex{-1};
    int scope{};
};

/** Draws package positions that no slot claims. */
void draw_positions(const state::build_data::scriptables::Snapshot& snapshot,
                    const server::activity::host::InstanceSnapshot& instance,
                    const Filters& filters) noexcept;

} // namespace sunrise::server::ui::activity_host::authored_anchors
