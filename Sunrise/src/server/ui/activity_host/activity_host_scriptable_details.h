#pragma once

#include <cstdint>

namespace sunrise::state::build_data::scriptables {
struct Slot;
struct Snapshot;
} // namespace sunrise::state::build_data::scriptables

namespace sunrise::server::activity::host {
struct InstanceSnapshot;
}

namespace sunrise::server::ui::activity_host::scriptable_details {

/** Draws every exact package descriptor owned by one selected slot. */
void draw_descriptors(const state::build_data::scriptables::Snapshot& snapshot,
                      const state::build_data::scriptables::Slot& slot) noexcept;

/** Draws explicit render toggles for exact type-4 descriptor-embedded positions. */
void draw_embedded_placements(const state::build_data::scriptables::Snapshot& snapshot,
                              const server::activity::host::InstanceSnapshot& instance,
                              const state::build_data::scriptables::Slot& slot) noexcept;

/** Draws explicit render toggles for exact type-23 slot-to-position links. */
void draw_type23_placements(const state::build_data::scriptables::Snapshot& snapshot,
                            const server::activity::host::InstanceSnapshot& instance,
                            const state::build_data::scriptables::Slot& slot) noexcept;

/** Draws native instances reached through exact type-23 package placements. */
void draw_live_instances(const state::build_data::scriptables::Snapshot& snapshot,
                         const state::build_data::scriptables::Slot& slot) noexcept;

/** Draws typed ClientRefs reached from one selected object's configs. */
void draw_references(const state::build_data::scriptables::Snapshot& snapshot,
                     std::uint32_t objectRow) noexcept;

} // namespace sunrise::server::ui::activity_host::scriptable_details
