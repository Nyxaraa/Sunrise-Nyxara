#pragma once

#include <cstdint>

namespace sunrise::state::build_data::scriptables {
struct Snapshot;
}

namespace sunrise::client::ui::activity::authored_placement_marker {
struct Anchor;
} // namespace sunrise::client::ui::activity::authored_placement_marker

namespace sunrise::client::ui::activity::package_type23_placement_marker_source {

/** Builds one exact selected type-23 slot-to-position anchor. */
[[nodiscard]] bool build(const state::build_data::scriptables::Snapshot& source,
                         std::uint32_t linkRow,
                         authored_placement_marker::Anchor& output) noexcept;

/** @return True while one retained descriptor-link identity remains exact and renderable. */
[[nodiscard]] bool current(const state::build_data::scriptables::Snapshot& source,
                           const authored_placement_marker::Anchor& anchor) noexcept;

/** @return The exact linked slot's strongest hash-name row. */
[[nodiscard]] std::uint32_t slot_name_row(const state::build_data::scriptables::Snapshot& source,
                                          const authored_placement_marker::Anchor& anchor) noexcept;

} // namespace sunrise::client::ui::activity::package_type23_placement_marker_source
