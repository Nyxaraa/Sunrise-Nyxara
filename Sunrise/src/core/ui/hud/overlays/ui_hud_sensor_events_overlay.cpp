#include "ui_hud_sensor_events_overlay.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <imgui.h>

#include "../../../../server/activity/host_runtime.h"

namespace sunrise::core::ui::hud::overlays::sensor_events {
namespace {

namespace host = server::activity::host;

/** The HUD stays compact; the Activity ingress window owns the full retained view. */
constexpr std::size_t kVisibleRows = 16;
constexpr std::uint32_t kPatchEpochMessageType = 52;
constexpr int kColumnCount = 3;
host::DiagnosticsSnapshot g_snapshot{};

} // namespace

/** Draws owned client-to-Activity-Host message history inside the current HUD window. */
void draw() noexcept {
    host::snapshot(g_snapshot);
    if (g_snapshot.clientMessageCount == 0) {
        ImGui::TextDisabled("no owned client -> Activity Host messages");
        return;
    }
    std::array<std::size_t, kVisibleRows> visibleIndices{};
    std::size_t visible = 0;
    for (std::size_t cursor = g_snapshot.clientMessageCount; cursor != 0 && visible < kVisibleRows;
         --cursor) {
        const std::size_t index = cursor - 1;
        if (g_snapshot.clientMessages[index].messageType == kPatchEpochMessageType) {
            continue;
        }
        visibleIndices[visible++] = index;
    }
    if (visible == 0) {
        ImGui::TextDisabled("no non-epoch Activity client messages");
        return;
    }
    if (!ImGui::BeginTable("##sunrise_hud_sensor_events",
                           kColumnCount,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        return;
    }
    ImGui::TableSetupColumn("#");
    ImGui::TableSetupColumn("packet");
    ImGui::TableSetupColumn("bytes");
    ImGui::TableHeadersRow();
    for (std::size_t visibleIndex = visible; visibleIndex != 0; --visibleIndex) {
        const host::ClientMessageRecord& message =
            g_snapshot.clientMessages[visibleIndices[visibleIndex - 1]];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%llu", static_cast<unsigned long long>(message.sequence));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(host::client_message_name(message.messageType));
        ImGui::TableNextColumn();
        ImGui::Text("%u", message.payloadBytes);
    }
    ImGui::EndTable();
    // Only a dropped message is a message the host never saw. The history ring keeps the newest
    // rows by design, and reporting the rows it rotated out read as packet loss.
    if (g_snapshot.droppedIngress != 0) {
        ImGui::TextDisabled("dropped %llu",
                            static_cast<unsigned long long>(g_snapshot.droppedIngress));
    }
}

} // namespace sunrise::core::ui::hud::overlays::sensor_events
