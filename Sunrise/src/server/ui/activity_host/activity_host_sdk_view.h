#pragma once

namespace sunrise::server::activity::host {
struct InstanceSnapshot;
}

namespace sunrise::server::ui::activity_host::sdk_view {

/** Draws the generated activity SDK for one selected Activity Host instance. */
void draw(bool& open, const server::activity::host::InstanceSnapshot* instance) noexcept;

} // namespace sunrise::server::ui::activity_host::sdk_view
