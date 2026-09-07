#include "activity_host_incident_editor.h"

#include <cstddef>
#include <imgui.h>

#include "../../../core/ui/components/section/ui_section_component.h"
#include "activity_host_table_layout.h"

namespace sunrise::server::ui::activity_host::incident_editor {
namespace {

namespace host = server::activity::host;
namespace section = core::ui::components::section;

/** One shared table style, so every table on this page reads the same. */
constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                        | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

/** Explains why an incident target is not exposed as a public selector. */
void draw_unmapped_target() noexcept {
    ImGui::TextDisabled("unmapped");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The packet names a table target. No name was recovered for it.");
    }
}

/** Draws retained incident metadata without exposing unnamed target or payload authoring. */
void draw_history(const state::activity::SessionBinding& binding,
                  const host::DiagnosticsSnapshot& snapshot) noexcept {
    std::size_t selectedCount = 0;
    for (std::size_t index = 0; index < snapshot.incidentCount; ++index) {
        selectedCount += same_binding(snapshot.incidents[index].binding, binding) ? 1U : 0U;
    }
    if (!ImGui::BeginTable(
            "##activity_host_incidents", 7, kTableFlags, table_layout::size(selectedCount, 16))) {
        return;
    }
    ImGui::TableSetupColumn("sequence");
    ImGui::TableSetupColumn("direction");
    ImGui::TableSetupColumn("target");
    ImGui::TableSetupColumn("extra targets");
    ImGui::TableSetupColumn("selector bytes");
    ImGui::TableSetupColumn("payload bytes");
    ImGui::TableSetupColumn("status");
    table_layout::frozen_headers();
    for (std::size_t index = 0; index < snapshot.incidentCount; ++index) {
        const host::IncidentRecord& record = snapshot.incidents[index];
        if (!same_binding(record.binding, binding)) {
            continue;
        }
        table_layout::next_row();
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(record.sequence));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(record.outbound ? "outbound" : "inbound");
        ImGui::TableNextColumn();
        draw_unmapped_target();
        ImGui::TableNextColumn();
        ImGui::Text("%u", record.incident.extraTargetCount);
        ImGui::TableNextColumn();
        ImGui::Text("%u", record.incident.selectorLength);
        ImGui::TableNextColumn();
        ImGui::Text("%u", record.incident.payloadLength);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(host::incident_status_name(record.status));
    }
    ImGui::EndTable();
    ImGui::TextDisabled("%zu selected  %zu retained", selectedCount, snapshot.incidentCount);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("dropped %llu; refused %llu; overwritten %llu",
                          static_cast<unsigned long long>(snapshot.droppedIncidents),
                          static_cast<unsigned long long>(snapshot.refusedIncidents),
                          static_cast<unsigned long long>(snapshot.overwrittenIncidents));
    }
}

} // namespace

/** Draws named incident status and read-only metadata for one activity generation. */
void draw(const state::activity::SessionBinding& binding,
          const host::InstanceSnapshot& instance,
          const host::DiagnosticsSnapshot& snapshot) noexcept {
    section::header("Incidents", nullptr);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("msg-19 arrivals. A script sees each one as an incident event.");
    }
    if (ImGui::TreeNodeEx("Technical details##incidents", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        ImGui::Text("queued r%llu, staged r%llu",
                    static_cast<unsigned long long>(instance.incidentRevision),
                    static_cast<unsigned long long>(instance.incidentTransportRevision));
        draw_history(binding, snapshot);
        ImGui::TreePop();
    }
}

} // namespace sunrise::server::ui::activity_host::incident_editor
