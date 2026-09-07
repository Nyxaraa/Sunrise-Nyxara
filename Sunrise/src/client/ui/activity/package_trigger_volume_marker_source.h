#pragma once

#include <cstdint>

namespace sunrise::state::build_data::scriptables {
struct Snapshot;
}

namespace sunrise::client::ui::activity::authored_placement_marker {
struct Anchor;
} // namespace sunrise::client::ui::activity::authored_placement_marker

namespace sunrise::client::ui::activity::package_trigger_volume_marker_source {

/** Builds one exact selected slot-owned trigger-volume anchor. */
[[nodiscard]] bool build(const state::build_data::scriptables::Snapshot& source,
                         std::uint32_t ownerRow,
                         std::uint32_t instanceRow,
                         authored_placement_marker::Anchor& output) noexcept;

/** @return True while one retained owner-and-instance identity remains exact and renderable. */
[[nodiscard]] bool current(const state::build_data::scriptables::Snapshot& source,
                           const authored_placement_marker::Anchor& anchor) noexcept;

/** @return The owning slot's strongest hash-name row, when the exact owner remains current. */
[[nodiscard]] std::uint32_t slot_name_row(const state::build_data::scriptables::Snapshot& source,
                                          const authored_placement_marker::Anchor& anchor) noexcept;

/** @return The source type-31 slot name row when exactly one incoming reference exists. */
[[nodiscard]] std::uint32_t
trigger_name_row(const state::build_data::scriptables::Snapshot& source,
                 const authored_placement_marker::Anchor& anchor) noexcept;

} // namespace sunrise::client::ui::activity::package_trigger_volume_marker_source
