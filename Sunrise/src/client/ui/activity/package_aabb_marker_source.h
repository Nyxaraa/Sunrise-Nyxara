#pragma once

#include <cstdint>

namespace sunrise::state::build_data::scriptables {
struct Snapshot;
}

namespace sunrise::client::ui::activity::authored_placement_marker {
struct Anchor;
}

namespace sunrise::client::ui::activity::package_aabb_marker_source {

/** Builds one exact world AABB anchor from its owner and instance row identity. */
[[nodiscard]] bool build(const state::build_data::scriptables::Snapshot& source,
                         std::uint32_t ownerRow,
                         std::uint32_t instanceRow,
                         authored_placement_marker::Anchor& output) noexcept;

/** @return True while one retained owner-and-instance identity remains current. */
[[nodiscard]] bool current(const state::build_data::scriptables::Snapshot& source,
                           const authored_placement_marker::Anchor& anchor) noexcept;

/** @return The strongest allowed table, resource, or package name row for one AABB. */
[[nodiscard]] std::uint32_t name_row(const state::build_data::scriptables::Snapshot& source,
                                     const authored_placement_marker::Anchor& anchor) noexcept;

} // namespace sunrise::client::ui::activity::package_aabb_marker_source
