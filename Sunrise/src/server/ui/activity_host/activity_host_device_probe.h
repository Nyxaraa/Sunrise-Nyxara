#pragma once

#include <cstdint>

namespace sunrise::state::build_data::scriptables {
struct Snapshot;
}

namespace sunrise::server::activity::host {
struct InstanceSnapshot;
}

namespace sunrise::server::ui::activity_host::device_probe {

/**
 * Draws a diagnostic control for one selected native device sensor.
 * @return True when the selected row is a type-23 device and owns this Actions surface.
 */
[[nodiscard]] bool draw(const state::build_data::scriptables::Snapshot& snapshot,
                        const server::activity::host::InstanceSnapshot& instance,
                        std::uint32_t objectRow,
                        std::uint32_t slotRow) noexcept;

} // namespace sunrise::server::ui::activity_host::device_probe
