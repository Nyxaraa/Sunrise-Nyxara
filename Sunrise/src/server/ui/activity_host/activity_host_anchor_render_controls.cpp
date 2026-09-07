#include "activity_host_anchor_render_controls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>

namespace sunrise::server::ui::activity_host::anchor_render_controls {
namespace {

namespace marker = client::ui::activity::authored_placement_marker;

struct ColorEditResult final {
    bool changed{};
    bool commit{};
};

/** Draws one compact, collision-proof palette swatch and its human source name. */
[[nodiscard]] ColorEditResult color_option(const char* id,
                                           const char* label,
                                           const char* tooltip,
                                           marker::MarkerColor& color) noexcept {
    ImGui::PushID(id);
    const bool changed = ImGui::ColorEdit4(
        "##color",
        color.data(),
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar);
    const bool colorHovered = ImGui::IsItemHovered();
    const bool commit = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    if (colorHovered || ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
    return {changed, commit};
}

} // namespace

/** Draws shared package-marker presentation controls and commits bounded options. */
void draw_options(marker::State& state) noexcept {
    marker::Options options = state.options;
    bool changed = false;
    bool commit = false;
    if (ImGui::Checkbox("Draw in world##world_markers", &options.enabled)) {
        changed = true;
        commit = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Draw positions and trigger shapes.");
    }

    // Combo entries in DisplayScope order; the index is stored as the option value.
    constexpr std::array<const char*, 3> displayScopes{"Ticked rows", "All rows", "Within radius"};
    int displayScope = static_cast<int>(options.displayScope);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0F);
    if (ImGui::Combo("Show##world_marker_display_scope",
                     &displayScope,
                     displayScopes.data(),
                     static_cast<int>(displayScopes.size()))) {
        options.displayScope = static_cast<marker::DisplayScope>(displayScope);
        changed = true;
        commit = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Which rows to draw.");
    }
    if (ImGui::TreeNodeEx("Render settings##world_markers", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (ImGui::Checkbox("Invert X##world_markers", &options.invertX)) {
            changed = true;
            commit = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Invert Y##world_markers", &options.invertY)) {
            changed = true;
            commit = true;
        }
        ImGui::SetNextItemWidth(120.0F);
        if (ImGui::InputFloat("Radius##world_markers", &options.nearbyRadius, 0.0F, 0.0F, "%.1f")) {
            if (!std::isfinite(options.nearbyRadius)) {
                options.nearbyRadius = marker::kMinimumNearbyRadius;
            }
            options.nearbyRadius = std::clamp(
                options.nearbyRadius, marker::kMinimumNearbyRadius, marker::kMaximumNearbyRadius);
            changed = true;
        }
        commit = ImGui::IsItemDeactivatedAfterEdit() || commit;

        // Combo entries in WorldGlyph order; the index is stored as the option value.
        constexpr std::array<const char*, 4> worldGlyphs{
            "Position cross", "XYZ axes", "Diagnostic box", "Diagnostic sphere"};
        int worldGlyph = static_cast<int>(options.worldGlyph);
        ImGui::SetNextItemWidth(150.0F);
        if (ImGui::Combo("Glyph##world_markers",
                         &worldGlyph,
                         worldGlyphs.data(),
                         static_cast<int>(worldGlyphs.size()))) {
            options.worldGlyph = static_cast<marker::WorldGlyph>(worldGlyph);
            changed = true;
            commit = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0F);
        if (ImGui::InputFloat("Size##world_markers", &options.worldGlyphSize, 0.0F, 0.0F, "%.2f")) {
            if (!std::isfinite(options.worldGlyphSize)) {
                options.worldGlyphSize = marker::kMinimumWorldGlyphSize;
            }
            options.worldGlyphSize = std::clamp(options.worldGlyphSize,
                                                marker::kMinimumWorldGlyphSize,
                                                marker::kMaximumWorldGlyphSize);
            changed = true;
        }
        commit = ImGui::IsItemDeactivatedAfterEdit() || commit;
        ImGui::SetNextItemWidth(120.0F);
        if (ImGui::SliderFloat("Line width##world_markers",
                               &options.worldLineWidth,
                               marker::kMinimumWorldLineWidth,
                               marker::kMaximumWorldLineWidth,
                               "%.1f px")) {
            options.worldLineWidth = std::clamp(options.worldLineWidth,
                                                marker::kMinimumWorldLineWidth,
                                                marker::kMaximumWorldLineWidth);
            changed = true;
        }
        commit = ImGui::IsItemDeactivatedAfterEdit() || commit;

        ImGui::TextDisabled("Colors");
        ColorEditResult color = color_option("object",
                                             "Object",
                                             "Embedded object position.",
                                             options.sourceColors.embeddedPlacement);
        changed = color.changed || changed;
        commit = color.commit || commit;
        ImGui::SameLine();
        color = color_option(
            "device", "Device", "Device position.", options.sourceColors.type23Placement);
        changed = color.changed || changed;
        commit = color.commit || commit;
        ImGui::SameLine();
        color = color_option(
            "trigger", "Trigger", "Trigger geometry.", options.sourceColors.triggerVolume);
        changed = color.changed || changed;
        commit = color.commit || commit;
        color = color_option("scenario",
                             "Scenario",
                             "Scenario-authored position.",
                             options.sourceColors.authoredPlacement);
        changed = color.changed || changed;
        commit = color.commit || commit;
        ImGui::SameLine();
        color = color_option("container",
                             "Container",
                             "Container placement.",
                             options.sourceColors.containerPlacement);
        changed = color.changed || changed;
        commit = color.commit || commit;
        ImGui::SameLine();
        color = color_option("sdk_squad",
                             "SDK squad",
                             "Direct generated SDK squad point.",
                             options.sourceColors.sdkSquadAnchor);
        changed = color.changed || changed;
        commit = color.commit || commit;
        if (ImGui::Checkbox("Always label##world_markers", &options.alwaysShowLabels)) {
            changed = true;
            commit = true;
        }
        ImGui::TreePop();
    }
    if (changed) {
        marker::preview_options(options);
        state.options = options;
    }
    if (commit) {
        marker::save_options();
    }
}

/** Draws shared marker selection and render-cap diagnostics for one exact context. */
void draw_status(const marker::Context& context, const marker::State& state) noexcept {
    const marker::RenderDiagnostics render = marker::render_diagnostics();
    if (state.options.enabled && (render.glyphsCapped || render.sourceScanCapped)) {
        ImGui::TextDisabled(
            "Draw limit: %zu shown, %zu rows checked.", render.glyphs, render.sourceRowsVisited);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The draw limit was reached. Some rows are missing.");
        }
    }
    const bool currentContext =
        state.selectionCount != 0 && marker::context_matches(state.context, context);
    if (state.selectionCount != 0 && !currentContext) {
        ImGui::TextDisabled("Ticked rows belong to another activity");
        return;
    }
    ImGui::TextDisabled("%zu ticked", state.selectionCount);
    if (state.selectionCapped) {
        ImGui::TextDisabled("tick limit reached");
    }
    if (state.options.enabled && state.selectionCount != 0
        && state.options.displayScope != marker::DisplayScope::selectedRows) {
        ImGui::TextDisabled("Show is not Ticked rows, so ticks do nothing here.");
    }
}

} // namespace sunrise::server::ui::activity_host::anchor_render_controls
