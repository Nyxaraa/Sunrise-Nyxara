#include "authored_placement_marker_draw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <string_view>

#include "../../../core/ui/scaling/dpi/ui_dpi_scaling.h"
#include "../../../middleware/content/packages/tables/container_placement_reader.h"
#include "../../../state/activity_sdk/runtime.h"
#include "package_aabb_marker_source.h"
#include "package_embedded_placement_marker_source.h"
#include "package_trigger_volume_marker_source.h"
#include "package_type23_placement_marker_source.h"

namespace sunrise::client::ui::activity::authored_placement_marker::draw_detail {
namespace {

namespace scaling = core::ui::scaling::dpi;
namespace package_aabb = package_aabb_marker_source;
namespace package_embedded = package_embedded_placement_marker_source;
namespace package_type23 = package_type23_placement_marker_source;
namespace package_trigger_volume = package_trigger_volume_marker_source;
namespace catalog = state::build_data::scriptables;
namespace sdk = state::activity_sdk;
namespace tables = middleware::content::packages::tables;

/** Foreground marker sizes are CSS-pixel presentation policy. */
constexpr ImU32 kShadowColor = IM_COL32(0, 0, 0, 220);
constexpr float kCrossRadius = 10.0F;
constexpr float kDiamondRadius = 7.0F;
constexpr float kLabelGap = 13.0F;
constexpr float kShadowOffset = 1.0F;
constexpr float kLineThickness = 2.0F;

/** Converts one selected source colour to ImGui's packed layout. */
[[nodiscard]] ImU32 marker_color(MarkerColor channels) noexcept {
    // Channels are held 0..1 and drawn as bytes.
    constexpr float scale = 255.0F;
    for (float& channel : channels) {
        channel = std::isfinite(channel) ? std::clamp(channel, 0.0F, 1.0F) : 1.0F;
    }
    return IM_COL32(static_cast<int>(channels[0] * scale + 0.5F),
                    static_cast<int>(channels[1] * scale + 0.5F),
                    static_cast<int>(channels[2] * scale + 0.5F),
                    static_cast<int>(channels[3] * scale + 0.5F));
}

/** @return The selected package name for an exact tag-name row, when unique. */
[[nodiscard]] const catalog::NameCandidate* selected_tag_name(const catalog::Snapshot& snapshot,
                                                              std::uint32_t row) noexcept {
    if (row >= snapshot.tagNames.size()) {
        return nullptr;
    }
    const catalog::TagName& name = snapshot.tagNames[row];
    return name.selectedCandidate < snapshot.nameCandidates.size()
               ? &snapshot.nameCandidates[name.selectedCandidate]
               : nullptr;
}

/** @return The selected strongest hash-name candidate, when that tier is unique. */
[[nodiscard]] const catalog::NameCandidate* selected_hash_name(const catalog::Snapshot& snapshot,
                                                               std::uint32_t row) noexcept {
    if (row >= snapshot.names.size()) {
        return nullptr;
    }
    const catalog::Name& name = snapshot.names[row];
    return name.selectedCandidate < snapshot.nameCandidates.size()
               ? &snapshot.nameCandidates[name.selectedCandidate]
               : nullptr;
}

/** @return The strongest exact package name attached to one placement row. */
[[nodiscard]] const catalog::NameCandidate* placement_name(const catalog::Snapshot& snapshot,
                                                           const Anchor& selection) noexcept {
    if (selection.sourceKind == AnchorSource::packageAabb) {
        return selected_tag_name(snapshot, package_aabb::name_row(snapshot, selection));
    }
    if (selection.sourceKind == AnchorSource::packageTriggerVolume) {
        const auto* const slotName = selected_hash_name(
            snapshot, package_trigger_volume::slot_name_row(snapshot, selection));
        if (slotName != nullptr) {
            return slotName;
        }
        if (selection.tableRow < snapshot.triggerVolumeTables.size()) {
            const auto* const configName = selected_tag_name(
                snapshot, snapshot.triggerVolumeTables[selection.tableRow].configNameRow);
            if (configName != nullptr) {
                return configName;
            }
        }
        if (selection.ownerRow < snapshot.triggerVolumeOwners.size()) {
            const auto& owner = snapshot.triggerVolumeOwners[selection.ownerRow];
            if (owner.objectRow < snapshot.objects.size()) {
                const auto& object = snapshot.objects[owner.objectRow];
                const auto* const objectName = selected_tag_name(snapshot, object.objectNameRow);
                if (objectName != nullptr) {
                    return objectName;
                }
                const auto* const registryName =
                    selected_tag_name(snapshot, object.registryNameRow);
                if (registryName != nullptr) {
                    return registryName;
                }
            }
        }
        if (selection.sourceRow < snapshot.triggerVolumeInstances.size()) {
            const auto& instance = snapshot.triggerVolumeInstances[selection.sourceRow];
            const auto* const className =
                selected_tag_name(snapshot, instance.classDefinitionNameRow);
            return className != nullptr
                       ? className
                       : selected_tag_name(snapshot, instance.shapeResourceNameRow);
        }
        return nullptr;
    }
    if (selection.sourceKind == AnchorSource::packageType23Placement) {
        return selected_hash_name(snapshot, package_type23::slot_name_row(snapshot, selection));
    }
    if (selection.sourceKind == AnchorSource::packageEmbeddedPlacement) {
        return selected_hash_name(snapshot, package_embedded::slot_name_row(snapshot, selection));
    }
    if (selection.sourceKind == AnchorSource::containerPlacement) {
        if (selection.sourceRow >= snapshot.containerPlacements.size()) {
            return nullptr;
        }
        const catalog::ContainerPlacement& placement =
            snapshot.containerPlacements[selection.sourceRow];
        const std::size_t configEnd =
            static_cast<std::size_t>(placement.firstConfig) + placement.configCount;
        if (configEnd <= snapshot.containerPlacementConfigs.size()) {
            for (std::size_t row = placement.firstConfig; row < configEnd; ++row) {
                const auto* name = selected_tag_name(
                    snapshot, snapshot.containerPlacementConfigs[row].configNameRow);
                if (name != nullptr) {
                    return name;
                }
            }
        }
        const auto* className = selected_tag_name(snapshot, placement.classListNameRow);
        if (className != nullptr) {
            return className;
        }
        if (placement.listRow < snapshot.containerPlacementLists.size()) {
            const auto& list = snapshot.containerPlacementLists[placement.listRow];
            const auto* resourceName = selected_tag_name(snapshot, list.resourceNameRow);
            return resourceName != nullptr ? resourceName
                                           : selected_tag_name(snapshot, list.objectListNameRow);
        }
        return nullptr;
    }
    if (selection.sourceRow >= snapshot.authoredPlacements.size()) {
        return nullptr;
    }
    const catalog::AuthoredPlacement& placement = snapshot.authoredPlacements[selection.sourceRow];
    const catalog::NameCandidate* const className =
        selected_tag_name(snapshot, placement.classListNameRow);
    return className != nullptr ? className
                                : selected_tag_name(snapshot, placement.objectListNameRow);
}

/** Writes the exact package-derived label for one resolved marker. */
void write_label(std::span<char> label,
                 const Anchor& selection,
                 const catalog::Snapshot& snapshot) noexcept {
    const catalog::NameCandidate* const name = placement_name(snapshot, selection);
    if (selection.sourceKind == AnchorSource::packageTriggerVolume) {
        const catalog::NameCandidate* const triggerName = selected_hash_name(
            snapshot, package_trigger_volume::trigger_name_row(snapshot, selection));
        const auto* const preferred =
            triggerName != nullptr && triggerName->length != 0 ? triggerName : name;
        if (preferred != nullptr && preferred->length != 0) {
            (void)std::snprintf(label.data(),
                                label.size(),
                                "%.*s",
                                static_cast<int>(preferred->length),
                                preferred->value.data());
        } else {
            const std::uint32_t key =
                selection.tableRow < snapshot.triggerVolumeTables.size()
                    ? snapshot.triggerVolumeTables[selection.tableRow].registryKey
                    : 0;
            (void)std::snprintf(label.data(), label.size(), "trigger %08X", key);
        }
        return;
    }
    if (selection.sourceKind == AnchorSource::packageType23Placement) {
        if (name != nullptr && name->length != 0) {
            (void)std::snprintf(label.data(),
                                label.size(),
                                "%.*s",
                                static_cast<int>(name->length),
                                name->value.data());
        } else {
            (void)std::snprintf(label.data(),
                                label.size(),
                                "type 23 slot %u",
                                static_cast<unsigned>(selection.slotRow));
        }
        return;
    }
    if (selection.sourceKind == AnchorSource::packageEmbeddedPlacement) {
        if (name != nullptr && name->length != 0) {
            (void)std::snprintf(label.data(),
                                label.size(),
                                "%.*s",
                                static_cast<int>(name->length),
                                name->value.data());
        } else {
            std::uint32_t nameHash = 0;
            const char* objectType = "unknown";
            if (selection.slotRow < snapshot.slots.size()) {
                nameHash = snapshot.slots[selection.slotRow].nameHash;
            }
            if (selection.sourceRow < snapshot.embeddedPlacementLinks.size()) {
                const auto& link = snapshot.embeddedPlacementLinks[selection.sourceRow];
                const auto* placement =
                    link.candidateCount == 1
                            && link.firstCandidate < snapshot.embeddedPlacements.size()
                        ? &snapshot.embeddedPlacements[link.firstCandidate]
                        : nullptr;
                if (placement != nullptr && placement->objectTypeRead) {
                    objectType = tables::placed_object_type_name(placement->objectType);
                }
            }
            (void)std::snprintf(label.data(),
                                label.size(),
                                "%s | type 4 0x%08X",
                                objectType,
                                static_cast<unsigned>(nameHash));
        }
        return;
    }
    if (selection.sourceKind == AnchorSource::packageAabb) {
        if (selection.sourceRow >= snapshot.staticSpatialInstances.size()) {
            return;
        }
        const auto& instance = snapshot.staticSpatialInstances[selection.sourceRow];
        if (name != nullptr && name->length != 0) {
            (void)std::snprintf(label.data(),
                                label.size(),
                                "%.*s [owner row %u instance %u row %u] -- package AABB; "
                                "unlinked to ClientRef/live render object",
                                static_cast<int>(name->length),
                                name->value.data(),
                                static_cast<unsigned>(selection.ownerRow),
                                static_cast<unsigned>(instance.instanceIndex),
                                static_cast<unsigned>(selection.sourceRow));
        } else {
            const std::uint32_t tableTag =
                selection.tableRow < snapshot.staticSpatialTables.size()
                    ? snapshot.staticSpatialTables[selection.tableRow].tableTag
                    : 0;
            (void)std::snprintf(label.data(),
                                label.size(),
                                "table 0x%08X resource 0x%08X [owner row %u instance %u row %u] "
                                "-- package AABB; unlinked to ClientRef/live render object",
                                static_cast<unsigned>(tableTag),
                                static_cast<unsigned>(selection.resourceTag),
                                static_cast<unsigned>(selection.ownerRow),
                                static_cast<unsigned>(instance.instanceIndex),
                                static_cast<unsigned>(selection.sourceRow));
        }
        return;
    }
    if (name != nullptr && name->length != 0) {
        if (selection.sourceKind == AnchorSource::containerPlacement
            && selection.sourceRow < snapshot.containerPlacements.size()) {
            const auto& placement = snapshot.containerPlacements[selection.sourceRow];
            const char* const objectType = tables::placed_object_type_name(placement.objectType);
            (void)std::snprintf(label.data(),
                                label.size(),
                                "%s | %.*s [%u]",
                                objectType,
                                static_cast<int>(name->length),
                                name->value.data(),
                                static_cast<unsigned>(selection.entryIndex));
        } else {
            (void)std::snprintf(label.data(),
                                label.size(),
                                "%.*s [%u]",
                                static_cast<int>(name->length),
                                name->value.data(),
                                static_cast<unsigned>(selection.entryIndex));
        }
        return;
    }
    if (selection.sourceKind == AnchorSource::containerPlacement) {
        const char* objectType = "unknown";
        if (selection.sourceRow < snapshot.containerPlacements.size()) {
            objectType = tables::placed_object_type_name(
                snapshot.containerPlacements[selection.sourceRow].objectType);
        }
        (void)std::snprintf(label.data(),
                            label.size(),
                            "%s | class 0x%08X [%u]",
                            objectType,
                            static_cast<unsigned>(selection.classListTag),
                            static_cast<unsigned>(selection.entryIndex));
    } else {
        (void)std::snprintf(label.data(),
                            label.size(),
                            "0x%08X[%u]",
                            static_cast<unsigned>(selection.objectListTag),
                            static_cast<unsigned>(selection.entryIndex));
    }
}

/** Draws the shared foreground glyph and one already-owned source label. */
void draw_marker(const core::ui::world_marker::ScreenPoint& point,
                 const core::ui::world_marker::ScreenPoint& labelPoint,
                 const Anchor& selection,
                 const Options& options,
                 bool drawPointGlyph,
                 bool drawLabel,
                 std::string_view label) noexcept {
    ImDrawList* const drawList = ImGui::GetForegroundDrawList();
    if (drawList == nullptr) {
        return;
    }
    const ImVec2 centre{point.x, point.y};
    const ImU32 color = marker_color(source_color(options, selection.sourceKind));
    if (drawPointGlyph) {
        const float cross = scaling::pixels(kCrossRadius);
        const float diamond = scaling::pixels(kDiamondRadius);
        const float thickness = scaling::pixels(kLineThickness);
        drawList->AddLine(
            {centre.x - cross, centre.y}, {centre.x + cross, centre.y}, color, thickness);
        drawList->AddLine(
            {centre.x, centre.y - cross}, {centre.x, centre.y + cross}, color, thickness);
        const std::array<ImVec2, 4> points{{{centre.x, centre.y - diamond},
                                            {centre.x + diamond, centre.y},
                                            {centre.x, centre.y + diamond},
                                            {centre.x - diamond, centre.y}}};
        drawList->AddPolyline(
            points.data(), static_cast<int>(points.size()), color, ImDrawFlags_Closed, thickness);
    }
    if (!drawLabel || label.empty()) {
        return;
    }
    const ImVec2 labelAt{labelPoint.x + scaling::pixels(kLabelGap),
                         labelPoint.y + scaling::pixels(2.0F)};
    const float shadow = scaling::pixels(kShadowOffset);
    drawList->AddText({labelAt.x + shadow, labelAt.y + shadow}, kShadowColor, label.data());
    drawList->AddText(labelAt, color, label.data());
}

} // namespace

/** Draws a cross, diamond, and exact package-row label at one screen point. */
void marker(const core::ui::world_marker::ScreenPoint& point,
            const core::ui::world_marker::ScreenPoint& labelPoint,
            const Anchor& selection,
            const catalog::Snapshot& snapshot,
            const Options& options,
            bool drawPointGlyph,
            bool drawLabel) noexcept {
    std::array<char, catalog::kNameCapacity * 3> label{};
    write_label(label, selection, snapshot);
    draw_marker(point, labelPoint, selection, options, drawPointGlyph, drawLabel, label.data());
}

/** Draws an SDK-owned point label without consulting dynamic authored-placement names. */
void sdk_squad_marker(const core::ui::world_marker::ScreenPoint& point,
                      const core::ui::world_marker::ScreenPoint& labelPoint,
                      const Anchor& selection,
                      const sdk::Catalog& catalog,
                      const Options& options,
                      bool drawPointGlyph,
                      bool drawLabel) noexcept {
    std::array<char, 512> label{};
    const auto squads = catalog.squads();
    const auto anchors = catalog.squad_anchors();
    const auto slots = catalog.slots();
    if (selection.ownerRow < squads.size() && selection.sourceRow < anchors.size()) {
        const sdk::format::Squad& squad = squads[selection.ownerRow];
        const sdk::format::SquadAnchor& anchor = anchors[selection.sourceRow];
        std::string_view name = catalog.string(squad.id);
        if (squad.slotIndex < slots.size()) {
            const sdk::format::Slot& slot = slots[squad.slotIndex];
            const std::string_view slotName = catalog.string(slot.name);
            const std::string_view slotId = catalog.string(slot.id);
            name = !slotName.empty() ? slotName : (!slotId.empty() ? slotId : name);
        }
        const std::string_view visibleName = name.empty() ? std::string_view{"SDK squad"} : name;
        (void)std::snprintf(label.data(),
                            label.size(),
                            "%.*s point %u | 0x%08X[%u]",
                            static_cast<int>(visibleName.size()),
                            visibleName.data(),
                            static_cast<unsigned>(anchor.pointOrdinal),
                            static_cast<unsigned>(anchor.objectListTag),
                            static_cast<unsigned>(anchor.placementOrdinal));
    }
    draw_marker(point, labelPoint, selection, options, drawPointGlyph, drawLabel, label.data());
}

} // namespace sunrise::client::ui::activity::authored_placement_marker::draw_detail
