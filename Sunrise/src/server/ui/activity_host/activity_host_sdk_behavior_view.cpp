// Behavior inventory pages and the installed compiled behavior roots.
// Drawn from the render thread only; the action results here take no lock.

#include "activity_host_sdk_behavior_view.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <string_view>
#include <vector>

#include "../../../middleware/content/packages/tables/slot_type.h"
#include "../../../state/activity_sdk/runtime.h"
#include "../../activity/activity_sdk_mission_runtime.h"
#include "activity_host_sdk_mission_text.h"
#include "activity_host_table_layout.h"

namespace sunrise::server::ui::activity_host::sdk_mission_view {

namespace mission = server::activity::activity_sdk_mission;
namespace sdk = state::activity_sdk;
namespace slot_tables = middleware::content::packages::tables;

namespace {
std::uint32_t g_taskActionOccurrence{sdk::format::kAbsentIndex};
std::uint32_t g_taskActionSlot{sdk::format::kAbsentIndex};
mission::SceneStatus g_taskActionStatus{mission::SceneStatus::ready};
bool g_hasTaskActionStatus{};
std::uint32_t g_behaviorActionOccurrence{sdk::format::kAbsentIndex};
std::uint32_t g_behaviorActionSlot{sdk::format::kAbsentIndex};
mission::SceneStatus g_behaviorActionStatus{mission::SceneStatus::ready};
bool g_hasBehaviorActionStatus{};

/** @return True when one slot type belongs to the named family's page. */
[[nodiscard]] bool behavior_slot(std::uint32_t type, BehaviorFamily family) noexcept {
    // Scenes, dialogue and HUD directives own their own pages. Engagement telemetry is an
    // observation, not an action, and belongs on no action page.
    return family == BehaviorFamily::objectives ? type == 3U || type == 38U
                                                : type == 5U || type == 6U;
}

/** Describes the proved server surface without offering a guessed mutation. */
[[nodiscard]] const char* behavior_support(const sdk::Catalog& catalog,
                                           const sdk::format::Slot& slot) noexcept {
    switch (slot.slotType) {
    case 3U:
        return "objective reset action; progress remains client-owned";
    case 38U:
        return sdk::slot_task_targets(catalog, slot).empty() ? "task target unresolved"
                                                             : "authored objective advance action";
    case 5U:
        return "authored sequence action";
    case 6U:
        return "authored cinematic action";
    case sdk::format::kAuthoredSceneSlotType:
        return "scene generation action";
    case sdk::format::kDialogueSlotType:
        return (slot.flags & sdk::format::kSlotDialogueCuesExact) != 0 ? "dialogue cue action"
                                                                       : "dialogue list unresolved";
    default:
        return "unsupported behavior action";
    }
}

} // namespace

/** Lists every state-local authored behavior row, including explicitly unsupported surfaces. */
void draw_behavior_inventory(const sdk::BoundView& view,
                             const mission::Snapshot& snapshot,
                             BehaviorFamily family) noexcept {
    if (view.catalog == nullptr) {
        return;
    }
    const sdk::Catalog& catalog = *view.catalog;
    const auto occurrences = catalog.occurrences();
    const auto objects = catalog.objects();
    const std::string_view query = search_text();
    std::size_t rows = 0;
    if (!ImGui::BeginTable("##sdk_behavior_inventory", 5, kSceneTableFlags)) {
        return;
    }
    ImGui::TableSetupColumn("behavior", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("object", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("slot");
    ImGui::TableSetupColumn("Auth schema");
    ImGui::TableSetupColumn("support", ImGuiTableColumnFlags_WidthStretch);
    table_layout::frozen_headers();
    for (std::uint32_t occurrenceRow = 0; occurrenceRow < occurrences.size(); ++occurrenceRow) {
        const sdk::format::Occurrence& occurrence = occurrences[occurrenceRow];
        if (occurrence.scenarioIndex != view.scenarioRow
            || occurrence.stateIndex != snapshot.plan.stateRow
            || occurrence.objectIndex >= objects.size()) {
            continue;
        }
        const sdk::format::Object& object = objects[occurrence.objectIndex];
        for (const sdk::format::Slot& slot : sdk::object_slots(catalog, object)) {
            const std::uint32_t slotRow =
                static_cast<std::uint32_t>(&slot - catalog.slots().data());
            if (!behavior_slot(slot.slotType, family)) {
                continue;
            }
            const char* const typeName =
                slot_tables::slot_type_name(static_cast<std::uint16_t>(slot.slotType));
            const char* const support = behavior_support(catalog, slot);
            if (!query.empty() && !contains_folded(catalog.string(slot.name), query)
                && !contains_folded(catalog.string(slot.id), query)
                && !contains_folded(catalog.string(object.id), query)
                && !contains_folded(typeName, query) && !contains_folded(support, query)) {
                continue;
            }
            ++rows;
            table_layout::next_row();
            ImGui::TableNextColumn();
            ImGui::Text("%s\n%.*s",
                        typeName,
                        print_length(display_text(catalog, slot.name)),
                        display_text(catalog, slot.name).data());
            ImGui::TableNextColumn();
            ImGui::Text("%.*s\noccurrence %u",
                        print_length(display_text(catalog, object.id)),
                        display_text(catalog, object.id).data(),
                        static_cast<unsigned>(occurrenceRow));
            ImGui::TableNextColumn();
            ImGui::Text("%u / type %u",
                        static_cast<unsigned>(slot.slotIndex),
                        static_cast<unsigned>(slot.slotType));
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", static_cast<unsigned>(slot.authSchema));
            ImGui::TableNextColumn();
            if (slot.slotType == sdk::format::kObjectiveSlotType) {
                const mission::SceneStatus availability =
                    mission::objective_reset_availability(view, occurrenceRow, slotRow);
                ImGui::PushID(static_cast<int>(occurrenceRow));
                ImGui::PushID(static_cast<int>(slotRow));
                ImGui::BeginDisabled(availability != mission::SceneStatus::ready);
                if (ImGui::Button("Reset all objectives")) {
                    g_behaviorActionOccurrence = occurrenceRow;
                    g_behaviorActionSlot = slotRow;
                    g_behaviorActionStatus =
                        mission::reset_objectives(view, occurrenceRow, slotRow);
                    g_hasBehaviorActionStatus = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", mission::status_name(availability));
                if (g_hasBehaviorActionStatus && g_behaviorActionOccurrence == occurrenceRow
                    && g_behaviorActionSlot == slotRow) {
                    ImGui::Text("Last action  %s", mission::status_name(g_behaviorActionStatus));
                }
                ImGui::PopID();
                ImGui::PopID();
            } else if (slot.slotType == sdk::format::kSequenceSlotType) {
                const mission::SceneStatus availability =
                    mission::sequence_availability(view, occurrenceRow, slotRow);
                ImGui::PushID(static_cast<int>(occurrenceRow));
                ImGui::PushID(static_cast<int>(slotRow));
                ImGui::BeginDisabled(availability != mission::SceneStatus::ready);
                if (ImGui::Button("Play sequence")) {
                    g_behaviorActionOccurrence = occurrenceRow;
                    g_behaviorActionSlot = slotRow;
                    g_behaviorActionStatus = mission::play_sequence(view, occurrenceRow, slotRow);
                    g_hasBehaviorActionStatus = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", mission::status_name(availability));
                if (g_hasBehaviorActionStatus && g_behaviorActionOccurrence == occurrenceRow
                    && g_behaviorActionSlot == slotRow) {
                    ImGui::Text("Last action  %s", mission::status_name(g_behaviorActionStatus));
                }
                ImGui::PopID();
                ImGui::PopID();
            } else if (slot.slotType == sdk::format::kCinematicSlotType) {
                const mission::SceneStatus availability =
                    mission::cinematic_availability(view, occurrenceRow, slotRow);
                ImGui::PushID(static_cast<int>(occurrenceRow));
                ImGui::PushID(static_cast<int>(slotRow));
                ImGui::BeginDisabled(availability != mission::SceneStatus::ready);
                if (ImGui::Button("Play cinematic")) {
                    g_behaviorActionOccurrence = occurrenceRow;
                    g_behaviorActionSlot = slotRow;
                    g_behaviorActionStatus =
                        mission::set_cinematic_active(view, occurrenceRow, slotRow, true);
                    g_hasBehaviorActionStatus = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop")) {
                    g_behaviorActionOccurrence = occurrenceRow;
                    g_behaviorActionSlot = slotRow;
                    g_behaviorActionStatus =
                        mission::set_cinematic_active(view, occurrenceRow, slotRow, false);
                    g_hasBehaviorActionStatus = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", mission::status_name(availability));
                if (g_hasBehaviorActionStatus && g_behaviorActionOccurrence == occurrenceRow
                    && g_behaviorActionSlot == slotRow) {
                    ImGui::Text("Last action  %s", mission::status_name(g_behaviorActionStatus));
                }
                ImGui::PopID();
                ImGui::PopID();
            } else if (slot.slotType == sdk::format::kTaskSlotType) {
                const auto targets = sdk::slot_task_targets(catalog, slot);
                if (!targets.empty()) {
                    const sdk::format::TaskTarget& target = targets.front();
                    const sdk::format::Slot* const objective =
                        sdk::task_linked_objective_slot(catalog, target);
                    const mission::SceneStatus availability =
                        mission::task_availability(view, occurrenceRow, slotRow);
                    ImGui::TextWrapped(
                        "objective %.*s  bit %u",
                        objective != nullptr ? print_length(display_text(catalog, objective->name))
                                             : 1,
                        objective != nullptr ? display_text(catalog, objective->name).data() : "-",
                        static_cast<unsigned>(target.bitIndex));
                    ImGui::PushID(static_cast<int>(occurrenceRow));
                    ImGui::PushID(static_cast<int>(slotRow));
                    ImGui::BeginDisabled(availability != mission::SceneStatus::ready);
                    if (ImGui::Button("Advance objective")) {
                        g_taskActionOccurrence = occurrenceRow;
                        g_taskActionSlot = slotRow;
                        g_taskActionStatus = mission::activate_task(view, occurrenceRow, slotRow);
                        g_hasTaskActionStatus = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", mission::status_name(availability));
                    if (g_hasTaskActionStatus && g_taskActionOccurrence == occurrenceRow
                        && g_taskActionSlot == slotRow) {
                        ImGui::Text("Last action  %s", mission::status_name(g_taskActionStatus));
                    }
                    ImGui::PopID();
                    ImGui::PopID();
                } else {
                    ImGui::TextWrapped("%s", support);
                }
            } else {
                ImGui::TextWrapped("%s", support);
            }
        }
    }
    ImGui::EndTable();
    if (rows == 0) {
        ImGui::TextDisabled("No row matches.");
    }
}

/** Lists every installed compiled root. A root tag is not a callable wire action. */
void draw_compiled_behavior_roots(const sdk::Catalog& catalog) noexcept {
    const auto programs = catalog.behavior_programs();
    const auto owners = catalog.behavior_owners();
    const auto bindings = catalog.behavior_activity_bindings();
    const std::string_view query = search_text();
    std::vector<std::uint32_t> visible{};
    std::vector<std::uint32_t> ownerCounts{};
    std::vector<std::uint32_t> bindingCounts{};
    std::vector<std::uint32_t> activeCounts{};
    try {
        visible.reserve(programs.size());
        ownerCounts.resize(programs.size());
        bindingCounts.resize(programs.size());
        activeCounts.resize(programs.size());
        for (const sdk::format::BehaviorOwner& owner : owners) {
            if (owner.programIndex < programs.size()) {
                ++ownerCounts[owner.programIndex];
                if (owner.submissionKind == sdk::format::BehaviorSubmissionKind::activeNative) {
                    ++activeCounts[owner.programIndex];
                }
            }
        }
        for (const sdk::format::BehaviorActivityBinding& binding : bindings) {
            if (binding.ownerIndex < owners.size()
                && owners[binding.ownerIndex].programIndex < programs.size()) {
                ++bindingCounts[owners[binding.ownerIndex].programIndex];
            }
        }
        std::array<char, 11> tagText{};
        for (std::uint32_t row = 0; row < programs.size(); ++row) {
            const sdk::format::BehaviorProgram& program = programs[row];
            const int written = std::snprintf(
                tagText.data(), tagText.size(), "0x%08X", static_cast<unsigned>(program.rootTag));
            if (query.empty()
                || (written > 0
                    && contains_folded(
                        std::string_view(tagText.data(), static_cast<std::size_t>(written)),
                        query))) {
                visible.push_back(row);
            }
        }
    } catch (...) {
        ImGui::TextDisabled("Compiled behavior list did not fit.");
        return;
    }
    ImGui::Text("%zu listed of %zu installed roots", visible.size(), programs.size());
    if (!ImGui::BeginTable("##sdk_compiled_behavior_roots", 8, kSceneTableFlags)) {
        return;
    }
    ImGui::TableSetupColumn("root");
    ImGui::TableSetupColumn("nodes");
    ImGui::TableSetupColumn("expressions");
    ImGui::TableSetupColumn("inputs");
    ImGui::TableSetupColumn("writes");
    ImGui::TableSetupColumn("owners");
    ImGui::TableSetupColumn("active");
    ImGui::TableSetupColumn("paths");
    table_layout::frozen_headers();
    ImGuiListClipper clipper{};
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const sdk::format::BehaviorProgram& row =
                programs[visible[static_cast<std::size_t>(index)]];
            table_layout::next_row();
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", static_cast<unsigned>(row.rootTag));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(row.nodeCount));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(row.expressionCount));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(row.inputs.count));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(row.channelWrites.count));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(ownerCounts[visible[index]]));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(activeCounts[visible[index]]));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(bindingCounts[visible[index]]));
        }
    }
    ImGui::EndTable();
}

/** Forgets the last objective, task and cinematic action result. */
void reset_behavior_action_status() noexcept {
    g_hasTaskActionStatus = false;
    g_hasBehaviorActionStatus = false;
}

} // namespace sunrise::server::ui::activity_host::sdk_mission_view
