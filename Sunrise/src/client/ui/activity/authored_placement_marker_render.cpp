#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <imgui.h>
#include <memory>

#include "../../../core/ui/scaling/dpi/ui_dpi_scaling.h"
#include "../../../core/ui/world_marker/projection.h"
#include "../../../server/bap/runtime.h"
#include "../../../state/activity/runtime.h"
#include "../../../state/activity_sdk/runtime.h"
#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "../../hooks/teleport/runtime.h"
#include "authored_placement_marker_draw.h"
#include "authored_placement_marker_internal.h"
#include "package_aabb_marker_source.h"
#include "package_embedded_placement_marker_source.h"
#include "package_trigger_volume_marker_source.h"
#include "package_type23_placement_marker_source.h"

namespace sunrise::client::ui::activity::authored_placement_marker {
namespace {

namespace projection = core::ui::world_marker;
namespace scaling = core::ui::scaling::dpi;
namespace package_aabb = package_aabb_marker_source;
namespace package_embedded = package_embedded_placement_marker_source;
namespace package_type23 = package_type23_placement_marker_source;
namespace package_trigger_volume = package_trigger_volume_marker_source;
namespace sdk_format = state::activity_sdk::format;
namespace teleport = sunrise::client::hooks::teleport;

/** Five degrees avoids labelling objects that are only near the screen centre. */
constexpr double kMinimumGazeCosine = 0.9961946980917455;
constexpr float kGazeViewportFraction = 0.04F;
constexpr float kMinimumGazeRadius = 24.0F;
constexpr float kMaximumGazeRadius = 96.0F;
/** Projected labels within this CSS-pixel radius share one stack. */
constexpr float kLabelCoincidenceRadius = 16.0F;

/** One deterministic screen-space stack shared by labels from every marker source. */
struct LabelLayout final {
    struct Cluster final {
        projection::ScreenPoint origin{};
        std::size_t nextRow{};
    };

    struct Entry final {
        projection::ScreenPoint point{};
        std::size_t cluster{};
    };

    std::array<Cluster, kLabelCapacity> clusters{};
    std::array<Entry, kLabelCapacity> entries{};
    std::size_t clusterCount{};
    std::size_t entryCount{};
};

/** Places one label in the first nearby cluster without moving its marker glyph. */
[[nodiscard]] projection::ScreenPoint stacked_label_point(LabelLayout& layout,
                                                          const projection::ScreenPoint& point,
                                                          float rowSpacing) noexcept {
    const float radius = scaling::pixels(kLabelCoincidenceRadius);
    const double radiusSquared = static_cast<double>(radius) * static_cast<double>(radius);
    std::size_t cluster = layout.clusters.size();
    for (std::size_t index = 0; index < layout.entryCount; ++index) {
        const double x = static_cast<double>(point.x) - layout.entries[index].point.x;
        const double y = static_cast<double>(point.y) - layout.entries[index].point.y;
        if (x * x + y * y <= radiusSquared) {
            cluster = layout.entries[index].cluster;
            break;
        }
    }
    if (cluster == layout.clusters.size()) {
        if (layout.clusterCount == layout.clusters.size()) {
            return point;
        }
        cluster = layout.clusterCount++;
        layout.clusters[cluster] = {point, 0};
    }
    projection::ScreenPoint output = layout.clusters[cluster].origin;
    output.y += rowSpacing * static_cast<float>(layout.clusters[cluster].nextRow++);
    if (layout.entryCount < layout.entries.size()) {
        layout.entries[layout.entryCount++] = {point, cluster};
    }
    return output;
}

/** @return True when one marker kind belongs to the selected SDK tab. */
[[nodiscard]] bool page_accepts(WorldPage page, AnchorSource source) noexcept {
    switch (page) {
    case WorldPage::none:
    case WorldPage::objects:
        return true;
    case WorldPage::devices:
        return source == AnchorSource::packageType23Placement
               || source == AnchorSource::packageEmbeddedPlacement;
    case WorldPage::triggers:
        return source == AnchorSource::packageTriggerVolume;
    case WorldPage::positions:
        return source == AnchorSource::authoredPlacement
               || source == AnchorSource::containerPlacement;
    case WorldPage::squads:
        return source == AnchorSource::sdkSquadAnchor;
    }
    return false;
}

/** @return True when the exact catalog shared by a selection set remains current. */
[[nodiscard]] bool catalog_context_current(const state::build_data::scriptables::Snapshot& catalog,
                                           const Context& context) noexcept {
    return context.catalogKind == CatalogKind::dynamicScriptables
           && catalog.revision == context.dynamicCatalogRevision
           && catalog.scenarioTag == context.scenarioTag
           && catalog.status == state::build_data::scriptables::BuildStatus::ready;
}

/** @return True while one exact package row still matches its retained identity. */
[[nodiscard]] bool catalog_anchor_current(const state::build_data::scriptables::Snapshot& catalog,
                                          const Anchor& selection) noexcept {
    if (selection.sourceKind == AnchorSource::packageAabb) {
        return false;
    }
    if (selection.sourceKind == AnchorSource::packageTriggerVolume) {
        return package_trigger_volume::current(catalog, selection);
    }
    if (selection.sourceKind == AnchorSource::packageType23Placement) {
        return package_type23::current(catalog, selection);
    }
    if (selection.sourceKind == AnchorSource::packageEmbeddedPlacement) {
        return package_embedded::current(catalog, selection);
    }
    if (selection.sourceKind == AnchorSource::containerPlacement) {
        if (selection.sourceRow >= catalog.containerPlacements.size()) {
            return false;
        }
        const state::build_data::scriptables::ContainerPlacement& anchor =
            catalog.containerPlacements[selection.sourceRow];
        return anchor.objectListTag == selection.objectListTag
               && anchor.classListTag == selection.classListTag
               && anchor.entryIndex == selection.entryIndex;
    }
    if (selection.bubbleRow >= catalog.bubbles.size() || selection.stateRow >= catalog.states.size()
        || selection.sourceRow >= catalog.authoredPlacements.size()) {
        return false;
    }
    const state::build_data::scriptables::Bubble& bubble = catalog.bubbles[selection.bubbleRow];
    const state::build_data::scriptables::State& owner = catalog.states[selection.stateRow];
    const state::build_data::scriptables::AuthoredPlacement& anchor =
        catalog.authoredPlacements[selection.sourceRow];
    return bubble.index == selection.bubbleIndex && owner.bubbleRow == selection.bubbleRow
           && owner.entryTag == selection.stateEntryTag
           && owner.sliceSetIndex == selection.sliceSetIndex
           && anchor.bubbleRow == selection.bubbleRow && anchor.stateRow == selection.stateRow
           && anchor.objectListTag == selection.objectListTag
           && anchor.classListTag == selection.classListTag
           && anchor.entryIndex == selection.entryIndex;
}

/** @return True while one retained SDK anchor is still the exact catalog row it named. */
[[nodiscard]] bool sdk_anchor_current(const sdk::Catalog& catalog,
                                      const Context& context,
                                      const Anchor& selection) noexcept {
    if (selection.sourceKind != AnchorSource::sdkSquadAnchor) {
        return false;
    }
    Anchor current{};
    if (!build_sdk_squad_anchor(
            catalog, context.sdkScenarioRow, selection.ownerRow, selection.sourceRow, current)) {
        return false;
    }
    return same_anchor_identity(current, selection) && current.slotRow == selection.slotRow
           && current.objectListTag == selection.objectListTag
           && current.entryIndex == selection.entryIndex
           && current.placementIdentifier == selection.placementIdentifier
           && current.position == selection.position;
}

/** Resolves and revalidates the current SDK mapping and ActivityClient generation. */
[[nodiscard]] bool current_sdk_catalog(const Context& context, sdk::Snapshot& catalog) noexcept {
    catalog.reset();
    if (!valid_context(context) || context.catalogKind != CatalogKind::activitySdk
        || sdk::status() != sdk::Status::ready) {
        return false;
    }
    catalog = sdk::snapshot();
    if (catalog == nullptr) {
        return false;
    }
    const std::span<const std::byte> digest = catalog->logical_ir_sha256();
    if (digest.size() != context.sdkLogicalIrSha256.size()
        || !std::equal(digest.begin(), digest.end(), context.sdkLogicalIrSha256.begin())) {
        catalog.reset();
        return false;
    }
    sdk::BoundView view{};
    view.catalog = catalog;
    view.binding = context.activity;
    view.activityClientGeneration = context.sdkActivityClientGeneration;
    view.activityRow = context.sdkActivityRow;
    view.scenarioRow = context.sdkScenarioRow;
    const sdk_format::Scenario* const scenario = sdk::bound_scenario(view);
    const sdk_format::Activity* const activity = sdk::bound_activity(view);
    if (scenario == nullptr || activity == nullptr || scenario->tag != context.scenarioTag
        || activity->scenarioIndex != context.sdkScenarioRow) {
        catalog.reset();
        return false;
    }
    server::bap::ActivityLinkView link{};
    (void)server::bap::activity_link_view(context.activity, link);
    if (sdk::revalidate(view, context.activity, link.matchingLinks, link.activityClientGeneration)
        != sdk::Status::ready) {
        catalog.reset();
        return false;
    }
    return true;
}

/** @return True when a projected point is inside the bounded centre-screen gaze cone. */
[[nodiscard]] bool camera_looks_at(const Anchor& anchor,
                                   const teleport::CameraPose& source,
                                   const projection::ScreenPoint& point,
                                   const projection::Viewport& area) noexcept {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(area.x)
        || !std::isfinite(area.y) || !std::isfinite(area.width) || !std::isfinite(area.height)
        || area.width <= 0.0F || area.height <= 0.0F) {
        return false;
    }
    const double centreX = static_cast<double>(area.x) + static_cast<double>(area.width) * 0.5;
    const double centreY = static_cast<double>(area.y) + static_cast<double>(area.height) * 0.5;
    const double screenX = static_cast<double>(point.x) - centreX;
    const double screenY = static_cast<double>(point.y) - centreY;
    const float gazeRadius = std::clamp((std::min)(area.width, area.height) * kGazeViewportFraction,
                                        scaling::pixels(kMinimumGazeRadius),
                                        scaling::pixels(kMaximumGazeRadius));
    const double gazeRadiusSquared = static_cast<double>(gazeRadius) * gazeRadius;
    if (screenX * screenX + screenY * screenY > gazeRadiusSquared) {
        return false;
    }

    double directionLengthSquared = 0.0;
    double forwardLengthSquared = 0.0;
    double alignment = 0.0;
    for (std::size_t lane = 0; lane < anchor.position.size(); ++lane) {
        if (!std::isfinite(anchor.position[lane]) || !std::isfinite(source.position[lane])
            || !std::isfinite(source.forward[lane])) {
            return false;
        }
        const double direction =
            static_cast<double>(anchor.position[lane]) - static_cast<double>(source.position[lane]);
        const double forward = static_cast<double>(source.forward[lane]);
        directionLengthSquared += direction * direction;
        forwardLengthSquared += forward * forward;
        alignment += direction * forward;
    }
    if (alignment <= 0.0 || directionLengthSquared <= 0.0 || forwardLengthSquared <= 0.0) {
        return false;
    }
    const double cosine = alignment / std::sqrt(directionLengthSquared * forwardLengthSquared);
    return std::isfinite(cosine) && cosine >= kMinimumGazeCosine;
}

/** Copies one current source row into the compact point-marker form. */
[[nodiscard]] bool catalog_anchor(const state::build_data::scriptables::Snapshot& catalog,
                                  AnchorSource sourceKind,
                                  std::uint32_t placementRow,
                                  std::uint32_t ownerRow,
                                  Anchor& output) noexcept {
    output = {};
    output.sourceKind = sourceKind;
    output.sourceRow = placementRow;
    output.ownerRow = ownerRow;
    if (sourceKind == AnchorSource::packageAabb) {
        return package_aabb::build(catalog, ownerRow, placementRow, output) && valid_anchor(output);
    }
    if (sourceKind == AnchorSource::packageTriggerVolume) {
        return package_trigger_volume::build(catalog, ownerRow, placementRow, output)
               && valid_anchor(output);
    }
    if (sourceKind == AnchorSource::packageType23Placement) {
        return package_type23::build(catalog, placementRow, output) && valid_anchor(output);
    }
    if (sourceKind == AnchorSource::packageEmbeddedPlacement) {
        return package_embedded::build(catalog, placementRow, output) && valid_anchor(output);
    }
    if (sourceKind == AnchorSource::containerPlacement) {
        if (placementRow >= catalog.containerPlacements.size()) {
            return false;
        }
        const state::build_data::scriptables::ContainerPlacement& placement =
            catalog.containerPlacements[placementRow];
        output.objectListTag = placement.objectListTag;
        output.classListTag = placement.classListTag;
        output.entryIndex = placement.entryIndex;
        output.position = placement.position;
        return valid_anchor(output);
    }
    if (placementRow >= catalog.authoredPlacements.size()) {
        return false;
    }
    const state::build_data::scriptables::AuthoredPlacement& placement =
        catalog.authoredPlacements[placementRow];
    if (placement.sourceObjectRow >= catalog.objects.size()
        || placement.stateRow >= catalog.states.size()
        || placement.bubbleRow >= catalog.bubbles.size()) {
        return false;
    }
    const state::build_data::scriptables::Object& object =
        catalog.objects[placement.sourceObjectRow];
    if (object.stateRow != placement.stateRow || object.bubbleRow != placement.bubbleRow) {
        return false;
    }
    const state::build_data::scriptables::State& owner = catalog.states[placement.stateRow];
    const state::build_data::scriptables::Bubble& bubble = catalog.bubbles[placement.bubbleRow];
    output.bubbleRow = placement.bubbleRow;
    output.bubbleIndex = bubble.index;
    output.stateRow = placement.stateRow;
    output.stateEntryTag = owner.entryTag;
    output.sliceSetIndex = owner.sliceSetIndex;
    output.objectListTag = placement.objectListTag;
    output.classListTag = placement.classListTag;
    output.entryIndex = placement.entryIndex;
    output.position = placement.position;
    return valid_anchor(output);
}

/** Draws one valid point when it passes radius and projection checks. */
[[nodiscard]] bool draw_anchor(const Anchor& anchor,
                               const state::build_data::scriptables::Snapshot& catalog,
                               const teleport::CameraPose& source,
                               const projection::Camera& camera,
                               const projection::Viewport& area,
                               const Options& options,
                               bool drawPointGlyph,
                               LabelLayout& labelLayout,
                               float labelRowSpacing,
                               std::size_t& labelled) noexcept {
    if (!catalog_anchor_current(catalog, anchor)) {
        return false;
    }
    projection::ScreenPoint point{};
    if (projection::project(anchor.position, camera, area, options.invertX, options.invertY, point)
        != projection::ProjectionStatus::visible) {
        return false;
    }
    const bool wantsLabel =
        options.alwaysShowLabels || camera_looks_at(anchor, source, point, area);
    const bool drawLabel = wantsLabel && labelled < kLabelCapacity;
    const projection::ScreenPoint labelPoint =
        drawLabel ? stacked_label_point(labelLayout, point, labelRowSpacing) : point;
    draw_detail::marker(point, labelPoint, anchor, catalog, options, drawPointGlyph, drawLabel);
    labelled += drawLabel ? 1U : 0U;
    return true;
}

/** Draws one direct SDK squad point after exact row revalidation and projection. */
[[nodiscard]] bool draw_sdk_anchor(const Anchor& anchor,
                                   const sdk::Catalog& catalog,
                                   const Context& context,
                                   const teleport::CameraPose& source,
                                   const projection::Camera& camera,
                                   const projection::Viewport& area,
                                   const Options& options,
                                   bool drawPointGlyph,
                                   LabelLayout& labelLayout,
                                   float labelRowSpacing,
                                   std::size_t& labelled) noexcept {
    if (!sdk_anchor_current(catalog, context, anchor)) {
        return false;
    }
    projection::ScreenPoint point{};
    if (projection::project(anchor.position, camera, area, options.invertX, options.invertY, point)
        != projection::ProjectionStatus::visible) {
        return false;
    }
    const bool wantsLabel =
        options.alwaysShowLabels || camera_looks_at(anchor, source, point, area);
    const bool drawLabel = wantsLabel && labelled < kLabelCapacity;
    const projection::ScreenPoint labelPoint =
        drawLabel ? stacked_label_point(labelLayout, point, labelRowSpacing) : point;
    draw_detail::sdk_squad_marker(
        point, labelPoint, anchor, catalog, options, drawPointGlyph, drawLabel);
    labelled += drawLabel ? 1U : 0U;
    return true;
}

} // namespace

/** Copies the exact bounded anchor set selected for this frame. */
bool render_set(RenderSet& output) noexcept {
    output = {};
    set_render_diagnostics(output);
    const State current = snapshot();
    const PageVisibility page = page_visibility(ImGui::GetFrameCount());
    const WorldPage worldPage = page.page;
    const DisplayScope displayScope = current.options.displayScope;
    if (!page.visible || !current.options.enabled
        || (displayScope == DisplayScope::selectedRows && current.selectionCount == 0)) {
        return false;
    }
    const std::shared_ptr<const PublishedRows> publishedRows = published_rows();
    const bool selectedScope = displayScope == DisplayScope::selectedRows;
    if (!selectedScope && publishedRows == nullptr) {
        return false;
    }
    const Context& context = selectedScope ? current.context : publishedRows->context;
    state::build_data::scriptables::SnapshotView dynamicCatalog{};
    sdk::Snapshot sdkCatalog{};
    bool contextCurrent = false;
    if (context.catalogKind == CatalogKind::dynamicScriptables) {
        dynamicCatalog = state::build_data::scriptables::snapshot();
        contextCurrent = dynamicCatalog != nullptr
                         && catalog_context_current(*dynamicCatalog, context)
                         && state::activity::binding_matches(context.activity);
    } else {
        contextCurrent = current_sdk_catalog(context, sdkCatalog);
    }
    if (!contextCurrent) {
        return false;
    }
    output.context = context;
    output.options = current.options;
    output.options.displayScope = displayScope;

    teleport::CameraPose source{};
    const bool radiusScope = displayScope == DisplayScope::nearbyRows;
    if (radiusScope && !teleport::camera_pose(source)) {
        return false;
    }
    const auto append = [&](const Anchor& anchor) noexcept {
        const bool rowCurrent =
            context.catalogKind == CatalogKind::activitySdk
                ? sdkCatalog != nullptr && sdk_anchor_current(*sdkCatalog, context, anchor)
                : dynamicCatalog != nullptr && catalog_anchor_current(*dynamicCatalog, anchor);
        if (anchor.sourceKind == AnchorSource::packageAabb
            || !page_accepts(worldPage, anchor.sourceKind)
            || !source_matches_context(context, anchor.sourceKind) || !rowCurrent
            || (radiusScope && !in_radius(anchor, source.position, current.options.nearbyRadius))) {
            return true;
        }
        if (output.count == output.anchors.size()) {
            output.capped = true;
            return false;
        }
        output.anchors[output.count++] = anchor;
        return true;
    };
    if (selectedScope) {
        const std::size_t sourceLimit =
            (std::min)(current.selectionCount, kRenderSourceVisitCapacity);
        for (std::size_t index = 0; index < sourceLimit; ++index) {
            ++output.sourceRowsVisited;
            if (!append(current.anchors[index])) {
                break;
            }
        }
        output.sourceScanCapped =
            current.selectionCount > sourceLimit && output.sourceRowsVisited == sourceLimit;
    } else if (publishedRows->source == PublishedSource::explicitRows) {
        const std::size_t sourceLimit =
            (std::min)(publishedRows->anchors.size(), kRenderSourceVisitCapacity);
        for (std::size_t index = 0; index < sourceLimit; ++index) {
            ++output.sourceRowsVisited;
            if (!append(publishedRows->anchors[index])) {
                break;
            }
        }
        output.sourceScanCapped =
            publishedRows->anchors.size() > sourceLimit && output.sourceRowsVisited == sourceLimit;
    } else if (context.catalogKind == CatalogKind::dynamicScriptables) {
        const bool includeAuthored = publishedRows->source != PublishedSource::containerOnly;
        const bool includeContainer = publishedRows->source != PublishedSource::authoredOnly;
        const std::size_t authoredCount =
            includeAuthored ? dynamicCatalog->authoredPlacements.size() : 0;
        const std::size_t containerCount =
            includeContainer ? dynamicCatalog->containerPlacements.size() : 0;
        const std::size_t totalCount = authoredCount + containerCount;
        const std::size_t sourceLimit = (std::min)(totalCount, kRenderSourceVisitCapacity);
        for (std::size_t sourceRow = 0; sourceRow < sourceLimit; ++sourceRow) {
            ++output.sourceRowsVisited;
            const bool authored = sourceRow < authoredCount;
            const std::size_t placementRow = authored ? sourceRow : sourceRow - authoredCount;
            Anchor anchor{};
            const AnchorSource sourceKind =
                authored ? AnchorSource::authoredPlacement : AnchorSource::containerPlacement;
            if (catalog_anchor(*dynamicCatalog,
                               sourceKind,
                               static_cast<std::uint32_t>(placementRow),
                               state::build_data::scriptables::kNoRow,
                               anchor)
                && !append(anchor)) {
                break;
            }
        }
        output.sourceScanCapped =
            totalCount > sourceLimit && output.sourceRowsVisited == sourceLimit;
    } else {
        return false;
    }
    set_render_diagnostics(output);
    return output.count != 0;
}

/** Draws labels and an optional depthless 2D fallback. */
bool draw(const RenderSet& current,
          const teleport::CameraPose& source,
          bool drawPointGlyphs) noexcept {
    state::build_data::scriptables::SnapshotView dynamicCatalog{};
    sdk::Snapshot sdkCatalog{};
    if (current.context.catalogKind == CatalogKind::dynamicScriptables) {
        dynamicCatalog = state::build_data::scriptables::snapshot();
        if (dynamicCatalog == nullptr
            || !catalog_context_current(*dynamicCatalog, current.context)) {
            return false;
        }
    } else if (!current_sdk_catalog(current.context, sdkCatalog)) {
        return false;
    }
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return false;
    }
    const projection::Camera camera{
        source.position, source.forward, source.up, source.horizontalFov, source.aspect};
    const projection::Viewport area{
        viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y};
    std::size_t drawn = 0;
    std::size_t labelled = 0;
    LabelLayout labelLayout{};
    const float labelRowSpacing = ImGui::GetTextLineHeightWithSpacing();
    for (std::size_t index = 0; index < current.count; ++index) {
        if (current.context.catalogKind == CatalogKind::activitySdk) {
            drawn += draw_sdk_anchor(current.anchors[index],
                                     *sdkCatalog,
                                     current.context,
                                     source,
                                     camera,
                                     area,
                                     current.options,
                                     drawPointGlyphs,
                                     labelLayout,
                                     labelRowSpacing,
                                     labelled)
                         ? 1U
                         : 0U;
        } else {
            drawn += draw_anchor(current.anchors[index],
                                 *dynamicCatalog,
                                 source,
                                 camera,
                                 area,
                                 current.options,
                                 drawPointGlyphs,
                                 labelLayout,
                                 labelRowSpacing,
                                 labelled)
                         ? 1U
                         : 0U;
        }
    }
    return drawn != 0;
}

} // namespace sunrise::client::ui::activity::authored_placement_marker
