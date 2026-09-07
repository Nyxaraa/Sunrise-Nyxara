#pragma once

#include "../../../core/ui/world_marker/projection.h"
#include "../../../state/build_data/scriptables/definition.h"
#include "authored_placement_marker.h"

namespace sunrise::client::ui::activity::authored_placement_marker::draw_detail {

/** Draws one resolved package marker with provenance-honest label text. */
void marker(const core::ui::world_marker::ScreenPoint& point,
            const core::ui::world_marker::ScreenPoint& labelPoint,
            const Anchor& selection,
            const state::build_data::scriptables::Snapshot& snapshot,
            const Options& options,
            bool drawPointGlyph,
            bool drawLabel) noexcept;

/** Draws one direct SDK squad point with a label owned only by generated SDK rows. */
void sdk_squad_marker(const core::ui::world_marker::ScreenPoint& point,
                      const core::ui::world_marker::ScreenPoint& labelPoint,
                      const Anchor& selection,
                      const state::activity_sdk::Catalog& catalog,
                      const Options& options,
                      bool drawPointGlyph,
                      bool drawLabel) noexcept;

} // namespace sunrise::client::ui::activity::authored_placement_marker::draw_detail
