#include "activity_host_scriptable_details.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <imgui.h>
#include <span>

#include "../../../client/hooks/world_objects/world_object_registry.h"
#include "../../../client/ui/activity/authored_placement_marker.h"
#include "../../../client/ui/activity/package_embedded_placement_marker_source.h"
#include "../../../client/ui/activity/package_type23_placement_marker_source.h"
#include "../../../middleware/content/packages/tables/container_placement_reader.h"
#include "../../../middleware/content/packages/tables/descriptor_embedded_placement_reader.h"
#include "../../../middleware/content/packages/tables/slot_descriptor_reader.h"
#include "../../../state/build_data/scriptables/definition.h"
#include "../../activity/host_runtime.h"
#include "activity_host_package_tag_names.h"
#include "activity_host_table_layout.h"

namespace sunrise::server::ui::activity_host::scriptable_details {
namespace {

namespace catalog = state::build_data::scriptables;
namespace live_objects = client::hooks::world_objects;
namespace marker = client::ui::activity::authored_placement_marker;
namespace embedded_source = client::ui::activity::package_embedded_placement_marker_source;
namespace placement_source = client::ui::activity::package_type23_placement_marker_source;
namespace tag_names = server::ui::activity_host::package_tag_names;
namespace tables = middleware::content::packages::tables;

/** One shared table style, so every table on this page reads the same. */
constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                        | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX
                                        | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

/** Draws a schema hash or its absent marker. */
void draw_schema(std::uint32_t schema) noexcept {
    if (schema == middleware::content::packages::tables::kAbsentSchema) {
        ImGui::TextDisabled("absent");
    } else {
        ImGui::Text("0x%08X", static_cast<unsigned>(schema));
    }
}

/** Builds the immutable activity and package identity for one manual render toggle. */
[[nodiscard]] marker::Context
marker_context(const catalog::Snapshot& snapshot,
               const server::activity::host::InstanceSnapshot& instance) noexcept {
    return {instance.binding, snapshot.revision, snapshot.scenarioTag};
}

} // namespace

/** Draws every exact package descriptor owned by one selected slot. */
void draw_descriptors(const catalog::Snapshot& snapshot, const catalog::Slot& slot) noexcept {
    if (slot.descriptorCount == 0) {
        ImGui::TextDisabled("no descriptor reached this slot");
        return;
    }
    if (!ImGui::BeginTable("##selected_descriptors",
                           7,
                           kTableFlags,
                           table_layout::size(slot.descriptorCount, 14))) {
        return;
    }
    ImGui::TableSetupColumn("config");
    ImGui::TableSetupColumn("offset");
    ImGui::TableSetupColumn("component");
    ImGui::TableSetupColumn("sense");
    ImGui::TableSetupColumn("auth");
    ImGui::TableSetupColumn("bubble");
    ImGui::TableSetupColumn("source");
    table_layout::frozen_headers();
    for (std::uint32_t offset = 0; offset < slot.descriptorCount; ++offset) {
        const catalog::Descriptor& row = snapshot.descriptors[slot.firstDescriptor + offset];
        table_layout::next_row();
        ImGui::TableNextColumn();
        tag_names::draw(snapshot, row.configNameRow, row.configTag);
        ImGui::TableNextColumn();
        ImGui::Text("+0x%X", static_cast<unsigned>(row.descriptorOffset));
        ImGui::TableNextColumn();
        ImGui::Text("0x%08X", static_cast<unsigned>(row.componentClass));
        ImGui::TableNextColumn();
        draw_schema(row.senseSchema);
        ImGui::TableNextColumn();
        draw_schema(row.authSchema);
        ImGui::TableNextColumn();
        ImGui::Text("%u", static_cast<unsigned>(row.bubbleIndex));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("package descriptor");
    }
    ImGui::EndTable();
}

/** Draws explicit render toggles for exact type-4 descriptor-embedded positions. */
void draw_embedded_placements(const catalog::Snapshot& snapshot,
                              const server::activity::host::InstanceSnapshot& instance,
                              const catalog::Slot& slot) noexcept {
    std::size_t count = 0;
    for (std::uint32_t offset = 0; offset < slot.descriptorCount; ++offset) {
        const std::size_t descriptorRow = static_cast<std::size_t>(slot.firstDescriptor) + offset;
        if (descriptorRow < snapshot.descriptors.size()
            && snapshot.descriptors[descriptorRow].embeddedPlacementLinkRow
                   < snapshot.embeddedPlacementLinks.size()) {
            ++count;
        }
    }
    if (count == 0) {
        return;
    }
    const marker::Context context = marker_context(snapshot, instance);
    marker::State selected = marker::snapshot();
    for (std::uint32_t offset = 0; offset < slot.descriptorCount; ++offset) {
        const std::size_t descriptorRow = static_cast<std::size_t>(slot.firstDescriptor) + offset;
        if (descriptorRow >= snapshot.descriptors.size()) {
            continue;
        }
        const catalog::Descriptor& descriptor = snapshot.descriptors[descriptorRow];
        if (descriptor.embeddedPlacementLinkRow >= snapshot.embeddedPlacementLinks.size()) {
            continue;
        }
        const std::uint32_t linkRow = descriptor.embeddedPlacementLinkRow;
        const catalog::EmbeddedPlacementLink& link = snapshot.embeddedPlacementLinks[linkRow];
        const catalog::EmbeddedPlacement* placement =
            link.candidateCount == 1 && link.firstCandidate < snapshot.embeddedPlacements.size()
                ? &snapshot.embeddedPlacements[link.firstCandidate]
                : nullptr;
        if (placement != nullptr && placement->linkRow != linkRow) {
            placement = nullptr;
        }
        marker::Anchor anchor{};
        const bool renderable = embedded_source::build(snapshot, linkRow, anchor);
        bool checked = renderable
                       && marker::contains(selected,
                                           context,
                                           marker::AnchorSource::packageEmbeddedPlacement,
                                           linkRow,
                                           anchor.ownerRow);
        ImGui::PushID("embedded_placement");
        ImGui::PushID(static_cast<int>(linkRow));
        ImGui::BeginDisabled(!renderable);
        if (ImGui::Checkbox("Draw", &checked)) {
            marker::toggle({context, anchor});
            selected = marker::snapshot();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (renderable && placement != nullptr) {
            const char* const objectType =
                placement->objectTypeRead ? tables::placed_object_type_name(placement->objectType)
                                          : "unknown";
            ImGui::Text("%s  %.2f, %.2f, %.2f",
                        objectType,
                        static_cast<double>(anchor.position[0]),
                        static_cast<double>(anchor.position[1]),
                        static_cast<double>(anchor.position[2]));
        } else if (link.complete) {
            ImGui::TextDisabled("%llu inline rows",
                                static_cast<unsigned long long>(link.declaredPlacementCount));
        } else {
            ImGui::TextDisabled("%llu declared; %u retained",
                                static_cast<unsigned long long>(link.declaredPlacementCount),
                                static_cast<unsigned>(link.candidateCount));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (placement != nullptr) {
                const char* const objectType =
                    placement->objectTypeRead
                        ? tables::placed_object_type_name(placement->objectType)
                        : "unknown";
                ImGui::SetTooltip("package descriptor 0x%08X+0x%X; native array +0x%zX; "
                                  "%llu declared, %u retained; entry %u at +0x%zX; "
                                  "class 0x%08X; object type %u (%s); id 0x%016llX",
                                  descriptor.configTag,
                                  descriptor.descriptorOffset,
                                  tables::kDescriptorEmbeddedPlacementArrayOffset,
                                  static_cast<unsigned long long>(link.declaredPlacementCount),
                                  static_cast<unsigned>(link.candidateCount),
                                  static_cast<unsigned>(placement->entryIndex),
                                  placement->sourceOffset,
                                  placement->classListTag,
                                  static_cast<unsigned>(placement->objectType),
                                  objectType,
                                  static_cast<unsigned long long>(placement->identifier));
            } else {
                ImGui::SetTooltip("package descriptor 0x%08X+0x%X; native array +0x%zX; "
                                  "%llu declared, %u retained; exact row selection unresolved",
                                  descriptor.configTag,
                                  descriptor.descriptorOffset,
                                  tables::kDescriptorEmbeddedPlacementArrayOffset,
                                  static_cast<unsigned long long>(link.declaredPlacementCount),
                                  static_cast<unsigned>(link.candidateCount));
            }
        }
        ImGui::PopID();
        ImGui::PopID();
    }
}

/** Draws explicit render toggles for exact type-23 slot-to-position links. */
void draw_type23_placements(const catalog::Snapshot& snapshot,
                            const server::activity::host::InstanceSnapshot& instance,
                            const catalog::Slot& slot) noexcept {
    std::size_t count = 0;
    for (std::uint32_t offset = 0; offset < slot.descriptorCount; ++offset) {
        const std::size_t descriptorRow = static_cast<std::size_t>(slot.firstDescriptor) + offset;
        if (descriptorRow < snapshot.descriptors.size()
            && snapshot.descriptors[descriptorRow].placementLinkRow
                   < snapshot.type23PlacementLinks.size()) {
            ++count;
        }
    }
    if (count == 0) {
        return;
    }
    const marker::Context context = marker_context(snapshot, instance);
    marker::State selected = marker::snapshot();
    const bool liveCaptureInstalled = live_objects::is_installed();
    for (std::uint32_t offset = 0; offset < slot.descriptorCount; ++offset) {
        const std::size_t descriptorRow = static_cast<std::size_t>(slot.firstDescriptor) + offset;
        if (descriptorRow >= snapshot.descriptors.size()) {
            continue;
        }
        const catalog::Descriptor& descriptor = snapshot.descriptors[descriptorRow];
        if (descriptor.placementLinkRow >= snapshot.type23PlacementLinks.size()) {
            continue;
        }
        const catalog::Type23PlacementLink& link =
            snapshot.type23PlacementLinks[descriptor.placementLinkRow];
        marker::Anchor anchor{};
        const bool renderable =
            placement_source::build(snapshot, descriptor.placementLinkRow, anchor);
        bool checked = renderable
                       && marker::contains(selected,
                                           context,
                                           marker::AnchorSource::packageType23Placement,
                                           descriptor.placementLinkRow,
                                           anchor.ownerRow);
        ImGui::PushID("type23_placement");
        ImGui::PushID(static_cast<int>(descriptor.placementLinkRow));
        ImGui::BeginDisabled(!renderable);
        if (ImGui::Checkbox("Draw", &checked)) {
            marker::toggle({context, anchor});
            selected = marker::snapshot();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        bool detailsHovered = false;
        if (renderable) {
            ImGui::Text("%.2f, %.2f, %.2f",
                        static_cast<double>(anchor.position[0]),
                        static_cast<double>(anchor.position[1]),
                        static_cast<double>(anchor.position[2]));
            detailsHovered = ImGui::IsItemHovered();
            const std::size_t liveCount = live_objects::find(
                anchor.objectListTag, anchor.entryIndex, std::span<live_objects::Instance>{});
            ImGui::SameLine();
            if (liveCount != 0) {
                ImGui::TextUnformatted("Live");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "%zu native instance%s", liveCount, liveCount == 1 ? "" : "s");
                }
            } else if (liveCaptureInstalled) {
                ImGui::TextDisabled("Not loaded");
            } else {
                ImGui::TextDisabled("Capture unavailable");
            }
        } else {
            ImGui::TextDisabled(
                "%u id matches, %u active", link.identityMatchCount, link.activeCandidateCount);
            detailsHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        }
        if (detailsHovered) {
            if (renderable) {
                ImGui::SetTooltip("authored-ID equality; list 0x%08X entry %u; class 0x%08X; "
                                  "id 0x%016llX; owner row %u (%u applicable) mask 0x%016llX",
                                  anchor.objectListTag,
                                  anchor.entryIndex,
                                  anchor.classListTag,
                                  static_cast<unsigned long long>(anchor.placementIdentifier),
                                  anchor.ownerRow,
                                  anchor.ownerMatchCount,
                                  static_cast<unsigned long long>(anchor.scenarioBubbleMask));
            } else {
                ImGui::SetTooltip("authored id 0x%016llX. The active placement must be unique.",
                                  static_cast<unsigned long long>(link.placementIdentifier));
            }
        }
        ImGui::PopID();
        ImGui::PopID();
    }
}

/** Draws native handles only within the selected object's technical disclosure. */
void draw_live_instances(const catalog::Snapshot& snapshot, const catalog::Slot& slot) noexcept {
    // Rows listed before the panel stops and reports a count; the list is a disclosure, not data.
    constexpr std::size_t kVisibleInstanceLimit = 16;
    std::size_t placementCount = 0;
    std::size_t liveCount = 0;
    for (std::uint32_t offset = 0; offset < slot.descriptorCount; ++offset) {
        const std::size_t descriptorRow = static_cast<std::size_t>(slot.firstDescriptor) + offset;
        if (descriptorRow >= snapshot.descriptors.size()) {
            continue;
        }
        const catalog::Descriptor& descriptor = snapshot.descriptors[descriptorRow];
        marker::Anchor anchor{};
        if (descriptor.placementLinkRow >= snapshot.type23PlacementLinks.size()
            || !placement_source::build(snapshot, descriptor.placementLinkRow, anchor)) {
            continue;
        }
        ++placementCount;
        liveCount += live_objects::find(
            anchor.objectListTag, anchor.entryIndex, std::span<live_objects::Instance>{});
    }
    if (placementCount == 0) {
        return;
    }

    ImGui::SeparatorText("Native instances");
    const live_objects::Diagnostics diagnostics = live_objects::diagnostics();
    if (!diagnostics.installed) {
        ImGui::TextDisabled("Capture unavailable");
        return;
    }
    if (liveCount == 0) {
        ImGui::TextDisabled("Not loaded");
    }
    if (!ImGui::BeginTable("##selected_live_instances",
                           3,
                           kTableFlags,
                           table_layout::size((std::max)(placementCount, liveCount), 8))) {
        return;
    }
    ImGui::TableSetupColumn("Package placement");
    ImGui::TableSetupColumn("Handle");
    ImGui::TableSetupColumn("Generation");
    table_layout::frozen_headers();
    for (std::uint32_t offset = 0; offset < slot.descriptorCount; ++offset) {
        const std::size_t descriptorRow = static_cast<std::size_t>(slot.firstDescriptor) + offset;
        if (descriptorRow >= snapshot.descriptors.size()) {
            continue;
        }
        const catalog::Descriptor& descriptor = snapshot.descriptors[descriptorRow];
        marker::Anchor anchor{};
        if (descriptor.placementLinkRow >= snapshot.type23PlacementLinks.size()
            || !placement_source::build(snapshot, descriptor.placementLinkRow, anchor)) {
            continue;
        }
        std::array<live_objects::Instance, kVisibleInstanceLimit> instances{};
        const std::size_t found =
            live_objects::find(anchor.objectListTag, anchor.entryIndex, instances);
        const std::size_t visible = (std::min)(found, instances.size());
        const std::size_t rows = (std::max)(visible, std::size_t{1});
        for (std::size_t index = 0; index < rows; ++index) {
            table_layout::next_row();
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X [%u]",
                        static_cast<unsigned>(anchor.objectListTag),
                        static_cast<unsigned>(anchor.entryIndex));
            ImGui::TableNextColumn();
            if (index < visible) {
                ImGui::Text("0x%08X", static_cast<unsigned>(instances[index].handle));
            } else {
                ImGui::TextDisabled("not loaded");
            }
            ImGui::TableNextColumn();
            if (index < visible) {
                ImGui::Text("0x%08X", static_cast<unsigned>(instances[index].generation));
            } else {
                ImGui::TextDisabled("-");
            }
        }
        if (found > visible) {
            table_layout::next_row();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%zu more instances", found - visible);
        }
    }
    ImGui::EndTable();
}

/** Draws typed ClientRefs reached from one selected object's configs. */
void draw_references(const catalog::Snapshot& snapshot, std::uint32_t objectRow) noexcept {
    std::size_t count = 0;
    for (const catalog::TypedReference& reference : snapshot.references) {
        count += reference.sourceObjectRow == objectRow ? 1U : 0U;
    }
    ImGui::Text("Typed ClientRefs (%zu)", count);
    if (count == 0) {
        ImGui::TextDisabled("none found in this object's configs");
        return;
    }
    if (!ImGui::BeginTable("##selected_refs", 6, kTableFlags, table_layout::size(count, 14))) {
        return;
    }
    ImGui::TableSetupColumn("config+offset");
    ImGui::TableSetupColumn("source slot");
    ImGui::TableSetupColumn("target key");
    ImGui::TableSetupColumn("type");
    ImGui::TableSetupColumn("index");
    ImGui::TableSetupColumn("same-registry join");
    table_layout::frozen_headers();
    for (const catalog::TypedReference& reference : snapshot.references) {
        if (reference.sourceObjectRow != objectRow) {
            continue;
        }
        table_layout::next_row();
        ImGui::TableNextColumn();
        tag_names::draw(snapshot, reference.sourceConfigNameRow, reference.sourceConfigTag);
        ImGui::SameLine();
        ImGui::TextDisabled("+0x%X", static_cast<unsigned>(reference.sourceOffset));
        ImGui::TableNextColumn();
        if (reference.sourceSlotRow == catalog::kNoRow) {
            ImGui::TextDisabled("config-level ambiguous");
        } else {
            ImGui::Text("%u",
                        static_cast<unsigned>(snapshot.slots[reference.sourceSlotRow].slotIndex));
        }
        ImGui::TableNextColumn();
        ImGui::Text("0x%08X", static_cast<unsigned>(reference.targetKey));
        ImGui::TableNextColumn();
        ImGui::Text("%u", static_cast<unsigned>(reference.targetSlotType));
        ImGui::TableNextColumn();
        ImGui::Text("%u", static_cast<unsigned>(reference.targetSlotIndex));
        ImGui::TableNextColumn();
        switch (reference.join) {
        case catalog::ReferenceJoin::exact:
            ImGui::Text("object row %u", static_cast<unsigned>(reference.targetObjectRow));
            break;
        case catalog::ReferenceJoin::ambiguous:
            ImGui::TextDisabled("ambiguous");
            break;
        case catalog::ReferenceJoin::unresolved:
            ImGui::TextDisabled("unresolved");
            break;
        }
    }
    ImGui::EndTable();
}

} // namespace sunrise::server::ui::activity_host::scriptable_details
