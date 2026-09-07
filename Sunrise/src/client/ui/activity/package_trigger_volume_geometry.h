#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../hooks/graphics/renderer/world_lines.h"

namespace sunrise::state::build_data::scriptables {
struct Snapshot;
struct TriggerVolumeInstance;
} // namespace sunrise::state::build_data::scriptables

namespace sunrise::client::ui::activity::package_trigger_volume_geometry {

/** Result of one bounded exact trigger-prism edge materialization. */
struct Result final {
    std::size_t count{};
    bool valid{};
    bool capacityExceeded{};
};

/** @return True only for the bit-exact identity SpawnEntry transform proved by the corpus. */
[[nodiscard]] bool
supported_transform(const state::build_data::scriptables::TriggerVolumeInstance& instance) noexcept;

/**
 * Materializes the boundary of one package-authored +Z-extruded triangle mesh.
 * Stored vertices are already world coordinates and are never transformed here.
 */
[[nodiscard]] Result build(const state::build_data::scriptables::Snapshot& source,
                           std::uint32_t instanceRow,
                           hooks::graphics::renderer::world_lines::Color color,
                           std::span<hooks::graphics::renderer::world_lines::Edge> output) noexcept;

} // namespace sunrise::client::ui::activity::package_trigger_volume_geometry
