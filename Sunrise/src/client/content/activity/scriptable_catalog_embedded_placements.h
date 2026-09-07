#pragma once

#include "../../../middleware/content/packages/reader/reader.h"
#include "../../../state/build_data/scriptables/definition.h"

namespace sunrise::client::content::activity::scriptables::internal {

using EmbeddedPlacementCancelCheck = bool (*)() noexcept;

/** Appends every bounded native placement array carried by a reached type-4 slot descriptor. */
[[nodiscard]] bool
append_embedded_placements(const middleware::content::packages::reader::Source& source,
                           middleware::content::packages::reader::Scratch& scratch,
                           state::build_data::scriptables::Snapshot& output,
                           EmbeddedPlacementCancelCheck cancel) noexcept;

} // namespace sunrise::client::content::activity::scriptables::internal
