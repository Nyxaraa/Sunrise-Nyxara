#include "activity_host_package_aabbs.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <vector>

#include "../../../client/ui/activity/authored_placement_marker.h"
#include "../../../client/ui/activity/package_aabb_marker_source.h"
#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "activity_host_package_tag_names.h"
#include "activity_host_table_layout.h"

namespace sunrise::server::ui::activity_host::package_aabbs {
namespace {

namespace aabb_source = client::ui::activity::package_aabb_marker_source;
namespace catalog = state::build_data::scriptables;
namespace marker = client::ui::activity::authored_placement_marker;
namespace tag_names = server::ui::activity_host::package_tag_names;

/** One shared table style, so every table on this page reads the same. */
constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                        | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX
                                        | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
/** One browser materialization cannot retain more package AABB identities than the catalog cap. */
constexpr std::size_t kBrowserRowCapacity = 262'144;

/** Exact package AABB identity remains the owner row paired with the instance row. */
struct BrowserRow final {
    std::uint32_t ownerRow{catalog::kNoRow};
    std::uint32_t instanceRow{catalog::kNoRow};
};

std::vector<BrowserRow> g_visible{};
std::uint64_t g_materializedRevision{};
std::array<char, 256> g_materializedFilter{};
int g_materializedBubble{-2};
int g_materializedState{-2};
int g_materializedScope{-1};
std::size_t g_sourceRowsVisited{};
bool g_browserCapped{};

/** @return The scenario bubble row selected by the browser, or no row. */
[[nodiscard]] std::uint32_t bubble_row(const catalog::Snapshot& snapshot,
                                       int bubbleIndex) noexcept {
    if (bubbleIndex < 0) {
        return catalog::kNoRow;
    }
    for (std::size_t row = 0; row < snapshot.bubbles.size(); ++row) {
        if (snapshot.bubbles[row].index == static_cast<std::uint32_t>(bubbleIndex)) {
            return static_cast<std::uint32_t>(row);
        }
    }
    return catalog::kNoRow;
}

/** @return True when one owner is joined and passes the optional scenario-bubble filter. */
[[nodiscard]] bool owner_matches(const catalog::Snapshot& snapshot,
                                 const catalog::StaticSpatialOwner& owner,
                                 std::uint32_t bubbleRow,
                                 bool bubbleRequested) noexcept {
    return owner.tableRow < snapshot.staticSpatialTables.size()
           && owner.placementRow < snapshot.containerPlacements.size()
           && (!bubbleRequested
               || (bubbleRow != catalog::kNoRow
                   && catalog::static_spatial_owner_applies(owner, bubbleRow)));
}

/** @return True when one numeric package identity passes the browser text filter. */
[[nodiscard]] bool identity_matches(const ImGuiTextFilter& filter,
                                    const catalog::StaticSpatialTable& table,
                                    const catalog::StaticSpatialOwner& owner,
                                    const catalog::StaticSpatialInstance& instance,
                                    const BrowserRow& row) noexcept {
    std::array<char, 320> identity{};
    (void)std::snprintf(identity.data(),
                        identity.size(),
                        "package AABB table 0x%08X bounds 0x%08X resource 0x%08X container "
                        "0x%08X list 0x%08X entry %u owner row %u instance %u unlinked ClientRef "
                        "instance row %u live render object",
                        table.tableTag,
                        table.boundsTag,
                        instance.resourceTag,
                        owner.containerTag,
                        owner.objectListTag,
                        owner.objectListEntry,
                        row.ownerRow,
                        instance.instanceIndex,
                        row.instanceRow);
    return filter.PassFilter(identity.data());
}

/** @return True when one AABB row passes only table, resource, and package identities. */
[[nodiscard]] bool row_matches(const catalog::Snapshot& snapshot,
                               const BrowserRow& row,
                               const authored_anchors::Filters& filters,
                               std::uint32_t bubbleRow) noexcept {
    if (row.ownerRow >= snapshot.staticSpatialOwners.size()
        || row.instanceRow >= snapshot.staticSpatialInstances.size() || filters.stateIndex >= 0
        || filters.scope > 0) {
        return false;
    }
    const auto& owner = snapshot.staticSpatialOwners[row.ownerRow];
    if (!owner_matches(snapshot, owner, bubbleRow, filters.bubbleIndex >= 0)) {
        return false;
    }
    const auto& table = snapshot.staticSpatialTables[owner.tableRow];
    const auto& instance = snapshot.staticSpatialInstances[row.instanceRow];
    if (instance.tableRow != owner.tableRow) {
        return false;
    }
    if (filters.text == nullptr || !filters.text->IsActive()) {
        return true;
    }
    const ImGuiTextFilter& filter = *filters.text;
    return tag_names::matches(snapshot, table.tableNameRow, filter)
           || tag_names::matches(snapshot, table.boundsNameRow, filter)
           || tag_names::matches(snapshot, instance.resourceNameRow, filter)
           || tag_names::matches(snapshot, owner.containerNameRow, filter)
           || tag_names::matches(snapshot, owner.objectListNameRow, filter)
           || tag_names::matches(snapshot, owner.parentNameRow, filter)
           || identity_matches(filter, table, owner, instance, row);
}

/** Rebuilds one bounded filtered owner-and-instance row set as an atomic vector swap. */
[[nodiscard]] bool materialize(const catalog::Snapshot& snapshot,
                               const authored_anchors::Filters& filters) noexcept {
    const char* const text = filters.text != nullptr ? filters.text->InputBuf : "";
    const bool current =
        g_materializedRevision == snapshot.revision && g_materializedBubble == filters.bubbleIndex
        && g_materializedState == filters.stateIndex && g_materializedScope == filters.scope
        && std::strncmp(g_materializedFilter.data(), text, g_materializedFilter.size()) == 0;
    if (current) {
        return true;
    }
    try {
        std::vector<BrowserRow> visible{};
        visible.reserve((std::min)(snapshot.staticSpatialInstances.size(), kBrowserRowCapacity));
        std::size_t visited = 0;
        bool capped = false;
        const std::uint32_t bubbleRow = bubble_row(snapshot, filters.bubbleIndex);
        for (std::size_t ownerRow = 0; ownerRow < snapshot.staticSpatialOwners.size() && !capped;
             ++ownerRow) {
            const auto& owner = snapshot.staticSpatialOwners[ownerRow];
            if (owner.tableRow >= snapshot.staticSpatialTables.size()) {
                continue;
            }
            const auto& table = snapshot.staticSpatialTables[owner.tableRow];
            const std::size_t first = table.firstInstance;
            const std::size_t count = table.instanceCount;
            if (!table.complete || first > snapshot.staticSpatialInstances.size()
                || count > snapshot.staticSpatialInstances.size() - first) {
                continue;
            }
            for (std::size_t offset = 0; offset < count; ++offset) {
                if (visited == kBrowserRowCapacity) {
                    capped = true;
                    break;
                }
                ++visited;
                const BrowserRow row{static_cast<std::uint32_t>(ownerRow),
                                     static_cast<std::uint32_t>(first + offset)};
                if (row_matches(snapshot, row, filters, bubbleRow)) {
                    visible.push_back(row);
                }
            }
        }
        g_visible.swap(visible);
        g_sourceRowsVisited = visited;
        g_browserCapped = capped;
        g_materializedRevision = snapshot.revision;
        g_materializedBubble = filters.bubbleIndex;
        g_materializedState = filters.stateIndex;
        g_materializedScope = filters.scope;
        g_materializedFilter = {};
        (void)std::snprintf(g_materializedFilter.data(), g_materializedFilter.size(), "%s", text);
        return true;
    } catch (...) {
        return false;
    }
}

/** Draws one exact owner-row and instance-row identity through the shared clipped table. */
void draw_row(const catalog::Snapshot& snapshot, const BrowserRow& row) noexcept {
    const auto& owner = snapshot.staticSpatialOwners[row.ownerRow];
    const auto& table = snapshot.staticSpatialTables[owner.tableRow];
    const auto& source = snapshot.staticSpatialInstances[row.instanceRow];
    marker::Anchor anchor{};
    if (!aabb_source::build(snapshot, row.ownerRow, row.instanceRow, anchor)) {
        return;
    }
    ImGui::PushID(static_cast<int>(row.ownerRow));
    ImGui::PushID(static_cast<int>(row.instanceRow));
    table_layout::next_row();
    ImGui::TableNextColumn();
    std::array<char, 64> identity{};
    (void)std::snprintf(identity.data(),
                        identity.size(),
                        "%u / %u (row %u)",
                        row.ownerRow,
                        source.instanceIndex,
                        row.instanceRow);
    ImGui::TextUnformatted(identity.data());
    ImGui::TableNextColumn();
    ImGui::Text("0x%llX", static_cast<unsigned long long>(owner.scenarioBubbleMask));
    ImGui::TableNextColumn();
    tag_names::draw(snapshot, owner.containerNameRow, owner.containerTag);
    ImGui::TableNextColumn();
    tag_names::draw(snapshot, owner.objectListNameRow, owner.objectListTag);
    ImGui::TextDisabled("entry %u", owner.objectListEntry);
    ImGui::TableNextColumn();
    tag_names::draw(snapshot, table.tableNameRow, table.tableTag);
    ImGui::TextDisabled("bounds:");
    ImGui::SameLine();
    tag_names::draw(snapshot, table.boundsNameRow, table.boundsTag);
    ImGui::TableNextColumn();
    tag_names::draw(snapshot, source.resourceNameRow, source.resourceTag);
    ImGui::TableNextColumn();
    ImGui::Text("%.3f, %.3f, %.3f -> %.3f, %.3f, %.3f",
                static_cast<double>(source.localMinimum[0]),
                static_cast<double>(source.localMinimum[1]),
                static_cast<double>(source.localMinimum[2]),
                static_cast<double>(source.localMaximum[0]),
                static_cast<double>(source.localMaximum[1]),
                static_cast<double>(source.localMaximum[2]));
    ImGui::TableNextColumn();
    ImGui::Text("%.3f, %.3f, %.3f -> %.3f, %.3f, %.3f",
                static_cast<double>(anchor.boundsMinimum[0]),
                static_cast<double>(anchor.boundsMinimum[1]),
                static_cast<double>(anchor.boundsMinimum[2]),
                static_cast<double>(anchor.boundsMaximum[0]),
                static_cast<double>(anchor.boundsMaximum[1]),
                static_cast<double>(anchor.boundsMaximum[2]));
    ImGui::TableNextColumn();
    ImGui::TextDisabled("read-only. It needs an exact slot link.");
    ImGui::PopID();
    ImGui::PopID();
}

/** Draws every filtered row through a clipper so large package tables stay bounded on screen. */
void draw_table(const catalog::Snapshot& snapshot) noexcept {
    if (!ImGui::BeginTable(
            "##package_aabbs", 9, kTableFlags, table_layout::size(g_visible.size()))) {
        return;
    }
    ImGui::TableSetupColumn("owner row / instance (row)");
    ImGui::TableSetupColumn("bubble mask");
    ImGui::TableSetupColumn("container");
    ImGui::TableSetupColumn("list / entry");
    ImGui::TableSetupColumn("table / bounds");
    ImGui::TableSetupColumn("resource");
    ImGui::TableSetupColumn("local min/max");
    ImGui::TableSetupColumn("world min/max");
    ImGui::TableSetupColumn("identity boundary");
    table_layout::frozen_headers();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(g_visible.size()));
    while (clipper.Step()) {
        for (int visible = clipper.DisplayStart; visible < clipper.DisplayEnd; ++visible) {
            draw_row(snapshot, g_visible[static_cast<std::size_t>(visible)]);
        }
    }
    ImGui::EndTable();
}

} // namespace

/** Browses package AABBs without submitting unassociated geometry to the renderer. */
void draw(const catalog::Snapshot& snapshot,
          const server::activity::host::InstanceSnapshot&,
          const authored_anchors::Filters& filters) noexcept {
    if (!materialize(snapshot, filters)) {
        ImGui::TextDisabled("package AABB row storage did not fit");
        return;
    }
    ImGui::TextDisabled("%zu shown, %zu rows checked", g_visible.size(), g_sourceRowsVisited);
    ImGui::SameLine();
    ImGui::TextDisabled("read-only until an exact slot link exists");
    if (g_browserCapped) {
        ImGui::TextDisabled("scan stopped at %zu rows", kBrowserRowCapacity);
    }
    if (!snapshot.staticSpatialComplete) {
        ImGui::TextDisabled("some links are incomplete: unresolved %llu, dropped %llu",
                            static_cast<unsigned long long>(snapshot.staticSpatialUnresolvedReads),
                            static_cast<unsigned long long>(snapshot.staticSpatialDropped));
    }
    draw_table(snapshot);
}

} // namespace sunrise::server::ui::activity_host::package_aabbs
