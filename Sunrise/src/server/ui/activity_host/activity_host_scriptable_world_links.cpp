// Slot-to-world anchor index for the package browser and the shared render controls.
// Drawn from the render thread only; the retained index takes no lock.

#include "activity_host_scriptable_world_links.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <imgui.h>
#include <span>
#include <vector>

#include "../../../client/ui/activity/authored_placement_marker.h"
#include "../../../client/ui/activity/package_embedded_placement_marker_source.h"
#include "../../../client/ui/activity/package_trigger_volume_marker_source.h"
#include "../../../client/ui/activity/package_type23_placement_marker_source.h"
#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "../../activity/host_runtime.h"
#include "activity_host_anchor_render_controls.h"

namespace sunrise::server::ui::activity_host::scriptable_browser {

namespace catalog = state::build_data::scriptables;
namespace embedded_source = client::ui::activity::package_embedded_placement_marker_source;
namespace host = server::activity::host;
namespace marker = client::ui::activity::authored_placement_marker;
namespace render_controls = server::ui::activity_host::anchor_render_controls;
namespace trigger_source = client::ui::activity::package_trigger_volume_marker_source;
namespace type23_source = client::ui::activity::package_type23_placement_marker_source;

namespace {
std::vector<SlotAnchor> g_renderableAnchors{};
std::vector<std::uint32_t> g_contextPositionCounts{};
std::vector<std::uint8_t> g_partialWorldLinks{};
/** Every anchor this scenario can draw. The search must never change it. */
std::vector<marker::Anchor> g_scenarioAnchors{};
/** Anchors owned by the rows the filters currently list, for the bulk-tick action only. */
std::vector<marker::Anchor> g_listedAnchors{};
std::uint64_t g_renderableRevision{};
bool g_listedAnchorsCapped{};

/** @return True when two retained anchors name the same renderer-owned package row. */
[[nodiscard]] bool same_anchor(const SlotAnchor& left, const SlotAnchor& right) noexcept {
    return left.slotRow == right.slotRow && left.anchor.sourceKind == right.anchor.sourceKind
           && left.anchor.sourceRow == right.anchor.sourceRow
           && left.anchor.ownerRow == right.anchor.ownerRow;
}

} // namespace

/** Builds the exact activity and catalog generation used by every object-page render action. */
[[nodiscard]] marker::Context marker_context(const catalog::Snapshot& snapshot,
                                             const host::InstanceSnapshot& instance) noexcept {
    return {instance.binding, snapshot.revision, snapshot.scenarioTag};
}

/** Rebuilds the flat slot-to-renderable index once for an immutable catalog revision. */
[[nodiscard]] bool rebuild_renderable_index(const catalog::Snapshot& snapshot) noexcept {
    if (g_renderableRevision == snapshot.revision) {
        return true;
    }
    try {
        std::vector<SlotAnchor> anchors{};
        anchors.reserve(snapshot.descriptors.size() + snapshot.triggerVolumeOwners.size());
        std::vector<std::uint32_t> contextCounts(snapshot.objects.size());
        std::vector<std::uint8_t> partial(snapshot.slots.size());

        for (const catalog::AuthoredPlacement& placement : snapshot.authoredPlacements) {
            if (placement.sourceObjectRow < contextCounts.size()) {
                ++contextCounts[placement.sourceObjectRow];
            }
        }
        for (const catalog::Descriptor& descriptor : snapshot.descriptors) {
            if (descriptor.slotRow >= snapshot.slots.size()) {
                continue;
            }
            if (descriptor.embeddedPlacementLinkRow < snapshot.embeddedPlacementLinks.size()) {
                marker::Anchor anchor{};
                if (embedded_source::build(snapshot, descriptor.embeddedPlacementLinkRow, anchor)) {
                    anchors.push_back({descriptor.slotRow, anchor});
                } else {
                    partial[descriptor.slotRow] = 1;
                }
            }
            if (descriptor.placementLinkRow < snapshot.type23PlacementLinks.size()) {
                marker::Anchor anchor{};
                if (type23_source::build(snapshot, descriptor.placementLinkRow, anchor)) {
                    anchors.push_back({descriptor.slotRow, anchor});
                } else {
                    partial[descriptor.slotRow] = 1;
                }
            }
        }
        for (std::size_t ownerRow = 0; ownerRow < snapshot.triggerVolumeOwners.size(); ++ownerRow) {
            const catalog::TriggerVolumeOwner& owner = snapshot.triggerVolumeOwners[ownerRow];
            if (owner.slotRow >= snapshot.slots.size()
                || owner.tableRow >= snapshot.triggerVolumeTables.size()) {
                continue;
            }
            const catalog::TriggerVolumeTable& table = snapshot.triggerVolumeTables[owner.tableRow];
            bool linked = false;
            const std::size_t first = table.firstInstance;
            const std::size_t count = table.instanceCount;
            if (first <= snapshot.triggerVolumeInstances.size()
                && count <= snapshot.triggerVolumeInstances.size() - first) {
                for (std::size_t offset = 0; offset < count; ++offset) {
                    marker::Anchor anchor{};
                    if (!trigger_source::build(snapshot,
                                               static_cast<std::uint32_t>(ownerRow),
                                               static_cast<std::uint32_t>(first + offset),
                                               anchor)) {
                        continue;
                    }
                    anchors.push_back({owner.slotRow, anchor});
                    linked = true;
                    const std::size_t incomingFirst = owner.firstIncomingReference;
                    const std::size_t incomingCount = owner.incomingReferenceCount;
                    if (incomingFirst <= snapshot.triggerVolumeIncomingReferences.size()
                        && incomingCount
                               <= snapshot.triggerVolumeIncomingReferences.size() - incomingFirst) {
                        for (std::size_t incomingOffset = 0; incomingOffset < incomingCount;
                             ++incomingOffset) {
                            const catalog::TriggerVolumeIncomingReference& incoming =
                                snapshot.triggerVolumeIncomingReferences[incomingFirst
                                                                         + incomingOffset];
                            if (incoming.sourceSlotRow < snapshot.slots.size()) {
                                anchors.push_back({incoming.sourceSlotRow, anchor});
                            }
                        }
                    }
                }
            }
            if (!linked) {
                if (owner.slotRow < partial.size()) {
                    partial[owner.slotRow] = 1;
                }
                const std::size_t incomingFirst = owner.firstIncomingReference;
                const std::size_t incomingCount = owner.incomingReferenceCount;
                if (incomingFirst <= snapshot.triggerVolumeIncomingReferences.size()
                    && incomingCount
                           <= snapshot.triggerVolumeIncomingReferences.size() - incomingFirst) {
                    for (std::size_t incomingOffset = 0; incomingOffset < incomingCount;
                         ++incomingOffset) {
                        const std::uint32_t incomingSlot =
                            snapshot.triggerVolumeIncomingReferences[incomingFirst + incomingOffset]
                                .sourceSlotRow;
                        if (incomingSlot < partial.size()) {
                            partial[incomingSlot] = 1;
                        }
                    }
                }
            }
        }
        std::sort(anchors.begin(),
                  anchors.end(),
                  [](const SlotAnchor& first, const SlotAnchor& second) noexcept {
                      if (first.slotRow != second.slotRow) {
                          return first.slotRow < second.slotRow;
                      }
                      if (first.anchor.sourceKind != second.anchor.sourceKind) {
                          return first.anchor.sourceKind < second.anchor.sourceKind;
                      }
                      if (first.anchor.sourceRow != second.anchor.sourceRow) {
                          return first.anchor.sourceRow < second.anchor.sourceRow;
                      }
                      return first.anchor.ownerRow < second.anchor.ownerRow;
                  });
        anchors.erase(std::unique(anchors.begin(), anchors.end(), &same_anchor), anchors.end());
        std::vector<marker::Anchor> scenarioAnchors{};
        scenarioAnchors.reserve(anchors.size());
        for (const SlotAnchor& row : anchors) {
            scenarioAnchors.push_back(row.anchor);
        }
        g_renderableAnchors.swap(anchors);
        g_scenarioAnchors.swap(scenarioAnchors);
        g_contextPositionCounts.swap(contextCounts);
        g_partialWorldLinks.swap(partial);
        g_listedAnchors.clear();
        g_listedAnchorsCapped = false;
        g_renderableRevision = snapshot.revision;
        return true;
    } catch (...) {
        return false;
    }
}

/** Returns the contiguous flat-index range for one exact slot row. */
[[nodiscard]] std::span<const SlotAnchor> slot_anchors(std::uint32_t slotRow) noexcept {
    const auto first = std::lower_bound(
        g_renderableAnchors.begin(),
        g_renderableAnchors.end(),
        slotRow,
        [](const SlotAnchor& row, std::uint32_t value) noexcept { return row.slotRow < value; });
    const auto last = std::upper_bound(
        first,
        g_renderableAnchors.end(),
        slotRow,
        [](std::uint32_t value, const SlotAnchor& row) noexcept { return value < row.slotRow; });
    const auto offset = first - g_renderableAnchors.begin();
    return {g_renderableAnchors.data() + offset, static_cast<std::size_t>(last - first)};
}

/** @return True when this object slot owns a position or shape accepted by the renderer. */
[[nodiscard]] bool slot_renderable(std::uint32_t slotRow) noexcept {
    return !slot_anchors(slotRow).empty();
}

/** @return Spatial facts linked by retained package rows, without inferring a live object. */
[[nodiscard]] WorldSummary world_summary(const catalog::Snapshot& snapshot,
                                         std::uint32_t slotRow) noexcept {
    WorldSummary output{};
    if (slotRow >= snapshot.slots.size()) {
        return output;
    }
    const catalog::Slot& slot = snapshot.slots[slotRow];
    if (slot.objectRow >= snapshot.objects.size()) {
        return output;
    }
    for (const SlotAnchor& row : slot_anchors(slotRow)) {
        if (row.anchor.sourceKind == marker::AnchorSource::packageTriggerVolume) {
            ++output.shapes;
        } else {
            ++output.linkedPositions;
        }
    }
    output.exact = output.linkedPositions != 0 || output.shapes != 0;
    output.contextPositions = slot.objectRow < g_contextPositionCounts.size()
                                  ? g_contextPositionCounts[slot.objectRow]
                                  : 0;
    output.partial = slotRow < g_partialWorldLinks.size() && g_partialWorldLinks[slotRow] != 0;
    return output;
}

/** @return The shortest honest link label for a world-object row. */
[[nodiscard]] const char* link_label(const WorldSummary& summary) noexcept {
    if (summary.exact) {
        return summary.partial ? "exact + partial" : "exact";
    }
    if (summary.contextPositions != 0) {
        return "context only";
    }
    return summary.partial ? "partial" : "none";
}

/** Counts exact anchors from one slot that are present in the copied marker selection. */
[[nodiscard]] std::size_t selected_anchor_count(std::span<const SlotAnchor> anchors,
                                                const marker::Context& context,
                                                const marker::State& selected) noexcept {
    return static_cast<std::size_t>(std::count_if(
        anchors.begin(), anchors.end(), [&context, &selected](const SlotAnchor& row) noexcept {
            return marker::contains(selected,
                                    context,
                                    row.anchor.sourceKind,
                                    row.anchor.sourceRow,
                                    row.anchor.ownerRow);
        }));
}

/** Ticks or clears every position and shape owned by one slot. It never changes what Show draws. */
void set_slot_rendering(std::span<const SlotAnchor> anchors,
                        const marker::Context& context,
                        bool enabled,
                        marker::State& selected) noexcept {
    if (anchors.empty()) {
        return;
    }
    for (const SlotAnchor& row : anchors) {
        const marker::State current = marker::snapshot();
        const bool present = marker::contains(
            current, context, row.anchor.sourceKind, row.anchor.sourceRow, row.anchor.ownerRow);
        if (present != enabled) {
            marker::toggle({context, row.anchor});
        }
    }
    selected = marker::snapshot();
}

/** Publishes every drawable row in this scenario and draws the shared render controls. */
void draw_world_render_controls(const catalog::Snapshot& snapshot,
                                const host::InstanceSnapshot& instance,
                                marker::State& selected) noexcept {
    const marker::Context context = marker_context(snapshot, instance);
    ImGui::PushID("world_object_render_controls");
    render_controls::draw_options(selected);
    const bool published =
        marker::publish_rows(context, g_scenarioAnchors, marker::PublishedSource::explicitRows);
    if (ImGui::Button("Tick listed")) {
        (void)marker::select_many(context, g_listedAnchors, g_listedAnchorsCapped);
        selected = marker::snapshot();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Ticks every position and shape owned by a listed row.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Untick all")) {
        marker::clear();
        selected = marker::snapshot();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu drawable in this scenario", g_scenarioAnchors.size());
    if (!published) {
        ImGui::TextDisabled("Draw source unavailable");
    } else if (g_listedAnchorsCapped) {
        ImGui::TextDisabled("Tick listed stops at %zu rows", marker::kRenderSourceVisitCapacity);
    }
    render_controls::draw_status(context, selected);
    ImGui::PopID();
}

/** @return How many slot-to-world anchors the retained index holds. */
std::size_t renderable_anchor_count() noexcept {
    return g_renderableAnchors.size();
}

/** Takes the anchors owned by the rows the filters now list. */
void swap_listed_anchors(std::vector<marker::Anchor>& anchors, bool capped) noexcept {
    g_listedAnchors.swap(anchors);
    g_listedAnchorsCapped = capped;
}

} // namespace sunrise::server::ui::activity_host::scriptable_browser
