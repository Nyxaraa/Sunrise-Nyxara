#include "authored_placement_marker.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <imgui.h>
#include <memory>

#include "../../../state/activity/runtime.h"
#include "../../../state/activity_sdk/runtime.h"
#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "authored_placement_marker_internal.h"
#include "authored_placement_marker_settings_store.h"

namespace sunrise::client::ui::activity::authored_placement_marker {
namespace {

namespace sdk_format = state::activity_sdk::format;

SRWLOCK g_lock{SRWLOCK_INIT};
State g_state{};
RenderDiagnostics g_renderDiagnostics{};
int g_visibleFrame{-1};
WorldPage g_worldPage{WorldPage::none};

std::shared_ptr<const PublishedRows> g_publishedRows{};

/** @return True when one fixed-size digest contains any identity byte. */
[[nodiscard]] bool has_digest(const std::array<std::byte, 32>& digest) noexcept {
    return std::any_of(digest.begin(), digest.end(), [](std::byte value) noexcept {
        return value != std::byte{};
    });
}

/** @return True when two values name the same activity and source-catalog generation. */
[[nodiscard]] bool same_context(const Context& left, const Context& right) noexcept {
    if (left.activity.sessionId != right.activity.sessionId
        || left.activity.createdRevision != right.activity.createdRevision
        || left.scenarioTag != right.scenarioTag || left.catalogKind != right.catalogKind) {
        return false;
    }
    if (left.catalogKind == CatalogKind::dynamicScriptables) {
        return left.dynamicCatalogRevision == right.dynamicCatalogRevision;
    }
    return left.sdkActivityRow == right.sdkActivityRow
           && left.sdkScenarioRow == right.sdkScenarioRow
           && left.sdkActivityClientGeneration == right.sdkActivityClientGeneration
           && left.sdkLogicalIrSha256 == right.sdkLogicalIrSha256;
}

/** @return True when one source uses its parent row as part of exact identity. */
[[nodiscard]] bool owner_scoped_source(AnchorSource source) noexcept {
    return source == AnchorSource::packageAabb || source == AnchorSource::packageTriggerVolume
           || source == AnchorSource::packageType23Placement
           || source == AnchorSource::packageEmbeddedPlacement
           || source == AnchorSource::sdkSquadAnchor;
}

} // namespace

/** @return The rows the active browser page published, or null when it published none. */
std::shared_ptr<const PublishedRows> published_rows() noexcept {
    AcquireSRWLockShared(&g_lock);
    const std::shared_ptr<const PublishedRows> copy = g_publishedRows;
    ReleaseSRWLockShared(&g_lock);
    return copy;
}

/** Replaces the published rows. A null value drops them. */
void set_published_rows(std::shared_ptr<const PublishedRows> rows) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_publishedRows = std::move(rows);
    ReleaseSRWLockExclusive(&g_lock);
}

/** @return The selected page and whether a marker-owning page drew in this ImGui frame. */
PageVisibility page_visibility(int frame) noexcept {
    PageVisibility output{};
    AcquireSRWLockShared(&g_lock);
    output.page = g_worldPage;
    output.visible = output.page != WorldPage::none || g_visibleFrame == frame;
    ReleaseSRWLockShared(&g_lock);
    return output;
}

/** Publishes one bounded render-set result for the workbench status line. */
void set_render_diagnostics(const RenderSet& source) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_renderDiagnostics.glyphs = source.count;
    g_renderDiagnostics.sourceRowsVisited = source.sourceRowsVisited;
    g_renderDiagnostics.glyphsCapped = source.capped;
    g_renderDiagnostics.sourceScanCapped = source.sourceScanCapped;
    ReleaseSRWLockExclusive(&g_lock);
}

/** @return True when a selection context has every required exact identity. */
bool valid_context(const Context& context) noexcept {
    if (context.activity.sessionId == state::activity::kAbsentSessionId
        || context.activity.createdRevision == state::activity::kInvalidRevision
        || context.scenarioTag == 0) {
        return false;
    }
    if (context.catalogKind == CatalogKind::dynamicScriptables) {
        return context.dynamicCatalogRevision != 0
               && context.sdkActivityRow == sdk_format::kAbsentIndex
               && context.sdkScenarioRow == sdk_format::kAbsentIndex
               && context.sdkActivityClientGeneration == 0
               && !has_digest(context.sdkLogicalIrSha256);
    }
    if (context.catalogKind != CatalogKind::activitySdk) {
        return false;
    }
    return context.dynamicCatalogRevision == 0 && context.sdkActivityRow != sdk_format::kAbsentIndex
           && context.sdkScenarioRow != sdk_format::kAbsentIndex
           && context.sdkActivityClientGeneration != 0 && has_digest(context.sdkLogicalIrSha256);
}

/** @return True when one source kind belongs to its explicitly named catalog family. */
bool source_matches_context(const Context& context, AnchorSource source) noexcept {
    return (context.catalogKind == CatalogKind::activitySdk)
           == (source == AnchorSource::sdkSquadAnchor);
}

/** @return True when one position anchor may enter the renderer. */
bool valid_anchor(const Anchor& anchor) noexcept {
    if (anchor.sourceKind == AnchorSource::sdkSquadAnchor) {
        return anchor.sourceRow != sdk_format::kAbsentIndex
               && anchor.ownerRow != sdk_format::kAbsentIndex
               && anchor.slotRow != sdk_format::kAbsentIndex && anchor.objectListTag != 0
               && anchor.objectListTag != sdk_format::kAbsentIndex
               && anchor.placementIdentifier != 0 && anchor.placementIdentifier != UINT64_MAX
               && std::all_of(anchor.position.begin(), anchor.position.end(), [](float value) {
                      return std::isfinite(value);
                  });
    }
    if (anchor.sourceKind == AnchorSource::packageTriggerVolume
        || anchor.sourceKind == AnchorSource::packageType23Placement
        || anchor.sourceKind == AnchorSource::packageEmbeddedPlacement) {
        return anchor.sourceRow != state::build_data::scriptables::kNoRow
               && anchor.ownerRow != state::build_data::scriptables::kNoRow
               && anchor.slotRow != state::build_data::scriptables::kNoRow && anchor.configTag != 0;
    }
    const bool sourceValid =
        anchor.sourceKind == AnchorSource::containerPlacement || anchor.stateEntryTag != 0;
    return anchor.sourceKind != AnchorSource::packageAabb && sourceValid
           && anchor.sourceRow != state::build_data::scriptables::kNoRow
           && anchor.objectListTag != 0;
}

/** @return True when two anchors name the same exact catalog row. */
bool same_anchor_identity(const Anchor& left, const Anchor& right) noexcept {
    return left.sourceKind == right.sourceKind && left.sourceRow == right.sourceRow
           && (!owner_scoped_source(left.sourceKind) || left.ownerRow == right.ownerRow);
}

/** Rebuilds one exact SDK-owned point from its global squad and anchor rows. */
bool build_sdk_squad_anchor(const sdk::Catalog& catalog,
                            std::uint32_t scenarioRow,
                            std::uint32_t squadRow,
                            std::uint32_t anchorRow,
                            Anchor& output) noexcept {
    output = {};
    const auto scenarios = catalog.scenarios();
    const auto squads = catalog.squads();
    const auto anchors = catalog.squad_anchors();
    const auto slots = catalog.slots();
    const auto objects = catalog.objects();
    const auto occurrences = catalog.occurrences();
    if (scenarioRow >= scenarios.size() || squadRow >= squads.size()
        || anchorRow >= anchors.size()) {
        return false;
    }
    const sdk_format::Squad& squad = squads[squadRow];
    if (squad.scenarioIndex != scenarioRow || squad.slotIndex >= slots.size()
        || squad.objectIndex >= objects.size() || squad.occurrenceIndex >= occurrences.size()
        || anchorRow < squad.anchors.first
        || anchorRow - squad.anchors.first >= squad.anchors.count) {
        return false;
    }
    const sdk_format::Slot& slot = slots[squad.slotIndex];
    const sdk_format::Occurrence& occurrence = occurrences[squad.occurrenceIndex];
    const sdk_format::SquadAnchor& anchor = anchors[anchorRow];
    const std::uint32_t childOrdinal = anchorRow - squad.anchors.first;
    if (slot.objectIndex != squad.objectIndex || occurrence.scenarioIndex != scenarioRow
        || occurrence.objectIndex != squad.objectIndex || anchor.squadIndex != squadRow
        || anchor.pointOrdinal != childOrdinal
        || (anchor.flags & sdk_format::kSquadAnchorExact) == 0) {
        return false;
    }

    output.sourceKind = AnchorSource::sdkSquadAnchor;
    output.sourceRow = anchorRow;
    output.ownerRow = squadRow;
    output.slotRow = squad.slotIndex;
    output.objectListTag = anchor.objectListTag;
    output.entryIndex = anchor.placementOrdinal;
    output.placementIdentifier = anchor.placedEntryIdentity;
    for (std::size_t lane = 0; lane < output.position.size(); ++lane) {
        output.position[lane] = std::bit_cast<float>(anchor.positionBits[lane]);
    }
    return valid_anchor(output);
}

/** Loads persistent presentation before any marker producer can publish session-bound rows. */
void initialize(void* module) noexcept {
    settings_store::initialize(module);
    State next{};
    next.options = settings_store::get();
    AcquireSRWLockExclusive(&g_lock);
    g_state = next;
    g_renderDiagnostics = {};
    g_visibleFrame = -1;
    g_worldPage = WorldPage::none;
    g_publishedRows.reset();
    ReleaseSRWLockExclusive(&g_lock);
}

/** Drops every process- and session-bound marker row after the UI stops. */
void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_state = {};
    g_renderDiagnostics = {};
    g_visibleFrame = -1;
    g_worldPage = WorldPage::none;
    g_publishedRows.reset();
    ReleaseSRWLockExclusive(&g_lock);
    settings_store::shutdown();
}

/** Selects one stable semantic colour without copying source identity into render rows. */
MarkerColor source_color(const Options& options, AnchorSource source) noexcept {
    switch (source) {
    case AnchorSource::authoredPlacement:
        return options.sourceColors.authoredPlacement;
    case AnchorSource::containerPlacement:
        return options.sourceColors.containerPlacement;
    case AnchorSource::packageTriggerVolume:
        return options.sourceColors.triggerVolume;
    case AnchorSource::packageType23Placement:
        return options.sourceColors.type23Placement;
    case AnchorSource::packageEmbeddedPlacement:
        return options.sourceColors.embeddedPlacement;
    case AnchorSource::sdkSquadAnchor:
        return options.sourceColors.sdkSquadAnchor;
    case AnchorSource::packageAabb:
        return {0.65F, 0.65F, 0.65F, 1.0F};
    }
    return {1.0F, 1.0F, 1.0F, 1.0F};
}

/** Compares the complete source-family-specific identity retained by two contexts. */
bool context_matches(const Context& left, const Context& right) noexcept {
    return same_context(left, right);
}

/** Captures one SDK mapping digest and the exact bound live identities it resolved against. */
bool sdk_context(const sdk::BoundView& view, Context& output) noexcept {
    output = {};
    const sdk_format::Activity* const activity = sdk::bound_activity(view);
    const sdk_format::Scenario* const scenario = sdk::bound_scenario(view);
    if (view.catalog == nullptr || activity == nullptr || scenario == nullptr
        || view.activityClientGeneration == 0 || scenario->tag == 0
        || activity->scenarioIndex != view.scenarioRow) {
        return false;
    }
    const std::span<const std::byte> digest = view.catalog->logical_ir_sha256();
    if (digest.size() != output.sdkLogicalIrSha256.size()) {
        return false;
    }
    output.activity = view.binding;
    output.dynamicCatalogRevision = 0;
    output.scenarioTag = scenario->tag;
    output.catalogKind = CatalogKind::activitySdk;
    output.sdkActivityRow = view.activityRow;
    output.sdkScenarioRow = view.scenarioRow;
    output.sdkActivityClientGeneration = view.activityClientGeneration;
    std::copy(digest.begin(), digest.end(), output.sdkLogicalIrSha256.begin());
    if (!valid_context(output)) {
        output = {};
        return false;
    }
    return true;
}

/** Builds one SDK point without translating it through the dynamic authored-placement catalog. */
bool sdk_squad_anchor(const sdk::BoundView& view,
                      std::uint32_t squadRow,
                      std::uint32_t anchorRow,
                      Anchor& output) noexcept {
    output = {};
    Context context{};
    if (!sdk_context(view, context)) {
        return false;
    }
    return build_sdk_squad_anchor(
        *view.catalog, context.sdkScenarioRow, squadRow, anchorRow, output);
}

/** Toggles one exact package row, replacing a selection set from a different context. */
void toggle(const Selection& selection) noexcept {
    if (!valid_context(selection.context) || !valid_anchor(selection.anchor)
        || !source_matches_context(selection.context, selection.anchor.sourceKind)) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (!same_context(g_state.context, selection.context)) {
        g_state.context = selection.context;
        g_state.selectionCount = 0;
        g_state.selectionCapped = false;
    }
    for (std::size_t index = 0; index < g_state.selectionCount; ++index) {
        if (!same_anchor_identity(g_state.anchors[index], selection.anchor)) {
            continue;
        }
        for (std::size_t cursor = index + 1; cursor < g_state.selectionCount; ++cursor) {
            g_state.anchors[cursor - 1] = g_state.anchors[cursor];
        }
        --g_state.selectionCount;
        g_state.selectionCapped = false;
        if (g_state.selectionCount == 0) {
            g_state.context = {};
        }
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (g_state.selectionCount == g_state.anchors.size()) {
        g_state.selectionCapped = true;
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    g_state.anchors[g_state.selectionCount++] = selection.anchor;
    ReleaseSRWLockExclusive(&g_lock);
}

/** Replaces the bounded selection set in one exact context. */
std::size_t
select_many(const Context& context, std::span<const Anchor> anchors, bool sourceCapped) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_state.context = {};
    g_state.selectionCount = 0;
    g_state.selectionCapped = sourceCapped || anchors.size() > g_state.anchors.size();
    if (valid_context(context)) {
        g_state.context = context;
        for (const Anchor& anchor :
             anchors.first((std::min)(anchors.size(), g_state.anchors.size()))) {
            if (!valid_anchor(anchor) || !source_matches_context(context, anchor.sourceKind)) {
                continue;
            }
            if (anchor.sourceKind == AnchorSource::packageAabb) {
                continue;
            }
            const bool duplicate = std::any_of(
                g_state.anchors.begin(),
                g_state.anchors.begin() + static_cast<std::ptrdiff_t>(g_state.selectionCount),
                [&anchor](const Anchor& current) noexcept {
                    return same_anchor_identity(current, anchor);
                });
            if (!duplicate) {
                g_state.anchors[g_state.selectionCount++] = anchor;
            }
        }
    }
    if (g_state.selectionCount == 0) {
        g_state.context = {};
    }
    const std::size_t count = g_state.selectionCount;
    ReleaseSRWLockExclusive(&g_lock);
    return count;
}

/** @return True when a copied marker state contains one exact package row in this context. */
bool contains(const State& state,
              const Context& context,
              AnchorSource sourceKind,
              std::uint32_t sourceRow,
              std::uint32_t ownerRow) noexcept {
    if (!same_context(state.context, context)) {
        return false;
    }
    return std::any_of(state.anchors.begin(),
                       state.anchors.begin() + static_cast<std::ptrdiff_t>(state.selectionCount),
                       [sourceKind, sourceRow, ownerRow](const Anchor& anchor) noexcept {
                           return anchor.sourceKind == sourceKind && anchor.sourceRow == sourceRow
                                  && (!owner_scoped_source(sourceKind)
                                      || anchor.ownerRow == ownerRow);
                       });
}

/** @return True when one authored anchor is inside a finite radius from the given origin. */
bool in_radius(const Anchor& anchor, const std::array<float, 3>& origin, float radius) noexcept {
    if (!std::isfinite(radius) || radius <= 0.0F) {
        return false;
    }
    double distanceSquared = 0.0;
    for (std::size_t lane = 0; lane < anchor.position.size(); ++lane) {
        if (!std::isfinite(anchor.position[lane]) || !std::isfinite(origin[lane])) {
            return false;
        }
        const double delta =
            static_cast<double>(anchor.position[lane]) - static_cast<double>(origin[lane]);
        distanceSquared += delta * delta;
    }
    const double radiusSquared = static_cast<double>(radius) * static_cast<double>(radius);
    return distanceSquared <= radiusSquared;
}

/** Clears every selected anchor without changing presentation choices. */
void clear() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_state.context = {};
    g_state.anchors = {};
    g_state.selectionCount = 0;
    g_state.selectionCapped = false;
    ReleaseSRWLockExclusive(&g_lock);
}

/** Copies bounded selection state for the workbench controls. */
State snapshot() noexcept {
    AcquireSRWLockShared(&g_lock);
    const State copy = g_state;
    ReleaseSRWLockShared(&g_lock);
    return copy;
}

/** Replaces and immediately saves marker presentation choices. */
void set_options(const Options& options) noexcept {
    preview_options(options);
    (void)settings_store::publish(options);
}

/** Applies an in-progress editor value without writing it to disk. */
void preview_options(const Options& options) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_state.options = options;
    ReleaseSRWLockExclusive(&g_lock);
}

/** Records that a marker-owning page is visible in the current ImGui frame. */
void show_for_frame() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_visibleFrame = ImGui::GetFrameCount();
    ReleaseSRWLockExclusive(&g_lock);
}

/** Records the selected SDK page. The published set belongs to a page, so a change drops it. */
void set_world_page(WorldPage page) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_worldPage != page) {
        g_worldPage = page;
        g_publishedRows.reset();
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Saves only presentation; catalog identities and selected rows remain process-local. */
void save_options() noexcept {
    AcquireSRWLockShared(&g_lock);
    const Options options = g_state.options;
    ReleaseSRWLockShared(&g_lock);
    (void)settings_store::publish(options);
}

/** Drops the published set. A page that cannot list its rows draws none. */
void publish_no_rows() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_publishedRows.reset();
    ReleaseSRWLockExclusive(&g_lock);
}

/** Publishes one page's drawable rows without changing the persistent marker selection. */
bool publish_rows(const Context& context,
                  std::span<const Anchor> anchors,
                  PublishedSource source) noexcept {
    if (!valid_context(context)
        || (context.catalogKind == CatalogKind::activitySdk
            && source != PublishedSource::explicitRows)
        || std::any_of(anchors.begin(), anchors.end(), [&context](const Anchor& anchor) noexcept {
               return anchor.sourceKind == AnchorSource::packageAabb || !valid_anchor(anchor)
                      || !source_matches_context(context, anchor.sourceKind);
           })) {
        return false;
    }
    show_for_frame();
    AcquireSRWLockShared(&g_lock);
    const std::shared_ptr<const PublishedRows> current = g_publishedRows;
    ReleaseSRWLockShared(&g_lock);
    if (current != nullptr && same_context(current->context, context) && current->source == source
        && current->anchors.size() == anchors.size()
        && std::equal(current->anchors.begin(),
                      current->anchors.end(),
                      anchors.begin(),
                      [](const Anchor& left, const Anchor& right) noexcept {
                          return same_anchor_identity(left, right)
                                 && left.objectListTag == right.objectListTag
                                 && left.classListTag == right.classListTag
                                 && left.entryIndex == right.entryIndex
                                 && left.tableRow == right.tableRow
                                 && left.resourceTag == right.resourceTag
                                 && left.ownerMatchCount == right.ownerMatchCount
                                 && left.scenarioBubbleMask == right.scenarioBubbleMask
                                 && left.placementIdentifier == right.placementIdentifier
                                 && left.position == right.position;
                      })) {
        return true;
    }
    try {
        auto next = std::make_shared<PublishedRows>();
        next->context = context;
        next->source = source;
        next->anchors.assign(anchors.begin(), anchors.end());
        AcquireSRWLockExclusive(&g_lock);
        g_publishedRows = std::move(next);
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    } catch (...) {
        return false;
    }
}

/** Copies the limits observed by the most recent render-set request. */
RenderDiagnostics render_diagnostics() noexcept {
    AcquireSRWLockShared(&g_lock);
    const RenderDiagnostics copy = g_renderDiagnostics;
    ReleaseSRWLockShared(&g_lock);
    return copy;
}

} // namespace sunrise::client::ui::activity::authored_placement_marker
