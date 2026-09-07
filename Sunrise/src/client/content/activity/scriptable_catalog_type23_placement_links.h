#pragma once

#include "../../../state/build_data/scriptables/definition.h"

namespace sunrise::client::content::activity::scriptables::internal {

using Type23PlacementCancelCheck = bool (*)() noexcept;

/** Builds a bounded equality graph from type-23 descriptor ids to authored placement ids. */
[[nodiscard]] bool append_type23_placement_links(state::build_data::scriptables::Snapshot& output,
                                                 Type23PlacementCancelCheck cancel) noexcept;

} // namespace sunrise::client::content::activity::scriptables::internal
