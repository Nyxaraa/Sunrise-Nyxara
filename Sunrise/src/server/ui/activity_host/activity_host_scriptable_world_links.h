#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../../../client/ui/activity/authored_placement_marker.h"
#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "../../activity/host_runtime.h"

namespace sunrise::server::ui::activity_host::scriptable_browser {

/** One exact slot-to-world association accepted by the depth-independent renderer. */
struct SlotAnchor final {
    std::uint32_t slotRow{};
    client::ui::activity::authored_placement_marker::Anchor anchor{};
};

/** Compact spatial facts already linked to one exact package slot. */
struct WorldSummary final {
    std::size_t linkedPositions{};
    std::size_t contextPositions{};
    std::size_t shapes{};
    bool exact{};
    bool partial{};
};

/** Builds the exact activity and catalog generation used by every object-page render action. */
[[nodiscard]] client::ui::activity::authored_placement_marker::Context
marker_context(const state::build_data::scriptables::Snapshot& snapshot,
               const server::activity::host::InstanceSnapshot& instance) noexcept;

/**
 * Rebuilds the flat slot-to-renderable index once for an immutable catalog revision.
 * @param snapshot Immutable catalog the index is built from.
 * @return False when the index did not fit; the previous index is then left intact.
 */
[[nodiscard]] bool
rebuild_renderable_index(const state::build_data::scriptables::Snapshot& snapshot) noexcept;

/** @return The contiguous flat-index range for one exact slot row. */
[[nodiscard]] std::span<const SlotAnchor> slot_anchors(std::uint32_t slotRow) noexcept;

/** @return True when this object slot owns a position or shape accepted by the renderer. */
[[nodiscard]] bool slot_renderable(std::uint32_t slotRow) noexcept;

/** @return Spatial facts linked by retained package rows, without inferring a live object. */
[[nodiscard]] WorldSummary world_summary(const state::build_data::scriptables::Snapshot& snapshot,
                                         std::uint32_t slotRow) noexcept;

/** @return The shortest honest link label for a world-object row. */
[[nodiscard]] const char* link_label(const WorldSummary& summary) noexcept;

/** @return How many slot-to-world anchors the retained index holds. */
[[nodiscard]] std::size_t renderable_anchor_count() noexcept;

/**
 * Takes the anchors owned by the rows the filters now list.
 * @param anchors Built list, swapped into the retained one and left holding the old entries.
 * @param capped True when the build stopped at the renderer's visit capacity.
 */
void swap_listed_anchors(
    std::vector<client::ui::activity::authored_placement_marker::Anchor>& anchors,
    bool capped) noexcept;

/** @return How many exact anchors from one slot are in the copied marker selection. */
[[nodiscard]] std::size_t selected_anchor_count(
    std::span<const SlotAnchor> anchors,
    const client::ui::activity::authored_placement_marker::Context& context,
    const client::ui::activity::authored_placement_marker::State& selected) noexcept;

/**
 * Ticks or clears every position and shape owned by one slot. It never changes what Show draws.
 * @param anchors Anchors of the one slot being changed.
 * @param context Activity and catalog generation the anchors belong to.
 * @param enabled Target tick state for every anchor.
 * @param selected Receives the marker selection after the change.
 */
void set_slot_rendering(std::span<const SlotAnchor> anchors,
                        const client::ui::activity::authored_placement_marker::Context& context,
                        bool enabled,
                        client::ui::activity::authored_placement_marker::State& selected) noexcept;

/**
 * Publishes every drawable row in this scenario and draws the shared render controls.
 * @param snapshot Immutable catalog the anchors were built from.
 * @param instance Selected activity the render context names.
 * @param selected Marker selection, updated by the tick and untick buttons.
 */
void draw_world_render_controls(
    const state::build_data::scriptables::Snapshot& snapshot,
    const server::activity::host::InstanceSnapshot& instance,
    client::ui::activity::authored_placement_marker::State& selected) noexcept;

} // namespace sunrise::server::ui::activity_host::scriptable_browser
