#include "activity_host_device_probe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <imgui.h>

#include "../../../core/ui/scaling/dpi/ui_dpi_scaling.h"
#include "../../../middleware/bap/activity_message/scriptable_auth_body.h"
#include "../../../middleware/content/packages/tables/region_reader.h"
#include "../../../middleware/content/packages/tables/slot_descriptor_reader.h"
#include "../../../state/activity_sdk/runtime.h"
#include "../../../state/build_data/scriptables/definition.h"
#include "../../activity/activity_sdk_device_runtime.h"
#include "../../activity/host_runtime.h"
#include "../../bap/runtime.h"

namespace sunrise::server::ui::activity_host::device_probe {
namespace {

namespace auth = middleware::bap::activity_message::scriptable_auth;
namespace catalog = state::build_data::scriptables;
namespace devices = server::activity::activity_sdk_devices;
namespace sdk = state::activity_sdk;
namespace scaling = core::ui::scaling::dpi;
namespace tables = middleware::content::packages::tables;

/** Lane names in type-23 channel order, one entry per channel. */
constexpr std::array<const char*, auth::kType23ChannelCount> kLaneLabels{
    "Position", "Power", "Lock"};
/** Lane help text in the same channel order. */
constexpr std::array<const char*, auth::kType23ChannelCount> kLaneHelp{
    "Where the device sits. What 0 and 1 mean is authored per device.",
    "Power. Position and lock only move while power is near 1.",
    "Lock. It only moves while power is near 1."};

std::uint64_t g_revision{};
std::uint32_t g_slotRow{catalog::kNoRow};
int g_lane{};
float g_value{1.0F};
bool g_snap{};
bool g_hasResult{};
devices::Status g_result{devices::Status::ready};

/** Resets controls and stale feedback when the selected package row changes. */
void bind_selection(std::uint64_t revision, std::uint32_t slotRow) noexcept {
    if (g_revision == revision && g_slotRow == slotRow) {
        return;
    }
    g_revision = revision;
    g_slotRow = slotRow;
    g_lane = 0;
    g_value = 1.0F;
    g_snap = false;
    g_hasResult = false;
    g_result = devices::Status::ready;
}

/** Maps one package row to its single generated SDK slot. */
[[nodiscard]] bool resolve(const catalog::Snapshot& snapshot,
                           const server::activity::host::InstanceSnapshot& instance,
                           std::uint32_t objectRow,
                           std::uint32_t slotRow,
                           sdk::BoundView& view,
                           std::uint32_t& generatedSlotRow,
                           const char*& status) noexcept {
    view = {};
    generatedSlotRow = sdk::format::kAbsentIndex;
    status = "generated SDK slot is unavailable";
    if (objectRow >= snapshot.objects.size() || slotRow >= snapshot.slots.size()) {
        status = "selected package row is incomplete";
        return false;
    }
    const catalog::Object& object = snapshot.objects[objectRow];
    const catalog::Slot& slot = snapshot.slots[slotRow];
    if (!object.complete || slot.objectRow != objectRow || slot.descriptorCount == 0
        || slot.firstDescriptor > snapshot.descriptors.size()
        || slot.descriptorCount > snapshot.descriptors.size() - slot.firstDescriptor) {
        status = "selected package row is incomplete";
        return false;
    }

    std::uint32_t authSchema = 0;
    for (std::uint32_t offset = 0; offset < slot.descriptorCount; ++offset) {
        const std::uint32_t current =
            snapshot.descriptors[slot.firstDescriptor + offset].authSchema;
        if (current == tables::kAbsentSchema || (offset != 0 && current != authSchema)) {
            status = "slot has no single Auth schema";
            return false;
        }
        authSchema = current;
    }
    const bool device = slot.slotType == auth::kType23SlotType && authSchema == auth::kType23Schema;
    const bool trigger =
        slot.slotType == auth::kType31SlotType && authSchema == auth::kType31Schema;
    if (!device && !trigger) {
        status = "slot has no native panel action";
        return false;
    }

    const sdk::Snapshot catalogView = sdk::snapshot();
    if (!catalogView) {
        status = "generated SDK is unavailable";
        return false;
    }
    server::bap::ActivityLinkView link{};
    if (!server::bap::activity_link_view(instance.binding, link) || link.matchingLinks != 1) {
        status = "activity link unavailable";
        return false;
    }
    const sdk::Selection selection{
        instance.binding, link.matchingLinks, link.activityClientGeneration};
    const sdk::Status bindStatus = sdk::resolve(catalogView, selection, view);
    if (bindStatus != sdk::Status::ready) {
        status = sdk::status_name(bindStatus);
        return false;
    }

    const sdk::Catalog& generated = *view.catalog;
    std::size_t matches = 0;
    const auto generatedObjects = generated.objects();
    const auto generatedSlots = generated.slots();
    for (const sdk::format::Object& candidateObject : generatedObjects) {
        if (candidateObject.objectTag != object.objectTag
            || candidateObject.objectKey != object.registryKey) {
            continue;
        }
        for (const sdk::format::Slot& candidate : sdk::object_slots(generated, candidateObject)) {
            if (candidate.slotIndex != slot.slotIndex || candidate.slotType != slot.slotType
                || candidate.authSchema != authSchema
                || (candidate.flags & sdk::format::kSlotSchemaJoinExact) == 0
                || &candidate < generatedSlots.data()
                || &candidate >= generatedSlots.data() + generatedSlots.size()) {
                continue;
            }
            generatedSlotRow = static_cast<std::uint32_t>(&candidate - generatedSlots.data());
            ++matches;
        }
    }
    if (matches != 1) {
        generatedSlotRow = sdk::format::kAbsentIndex;
        status =
            matches == 0 ? "generated SDK slot is unavailable" : "generated SDK slot is ambiguous";
        return false;
    }
    status = "ready";
    return true;
}

} // namespace

/** Draws one exact named native device control. */
bool draw(const catalog::Snapshot& snapshot,
          const server::activity::host::InstanceSnapshot& instance,
          std::uint32_t objectRow,
          std::uint32_t slotRow) noexcept {
    if (slotRow >= snapshot.slots.size()) {
        return false;
    }
    const std::uint16_t slotType = snapshot.slots[slotRow].slotType;
    if (slotType != auth::kType23SlotType && slotType != auth::kType31SlotType) {
        return false;
    }
    bind_selection(snapshot.revision, slotRow);

    sdk::BoundView view{};
    std::uint32_t generatedSlotRow = sdk::format::kAbsentIndex;
    const char* status = nullptr;
    const bool mapped =
        resolve(snapshot, instance, objectRow, slotRow, view, generatedSlotRow, status);

    if (slotType == auth::kType31SlotType) {
        const devices::Status available =
            mapped ? devices::trigger_availability(view, generatedSlotRow)
                   : devices::Status::invalidSlot;
        const bool ready = mapped && available == devices::Status::ready;
        ImGui::BeginDisabled(instance.outputPending || !ready);
        if (ImGui::Button("Fire trigger")) {
            g_result = devices::fire_trigger(view, generatedSlotRow);
            g_hasResult = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (instance.outputPending) {
            ImGui::TextDisabled("another action is queued");
        } else if (!mapped) {
            ImGui::TextDisabled("%s", status);
        } else if (!ready) {
            ImGui::TextDisabled("%s", devices::status_name(available));
        } else if (g_hasResult) {
            ImGui::TextDisabled("%s", devices::status_name(g_result));
        } else {
            ImGui::TextDisabled("ready");
        }
        ImGui::TextDisabled("Runs the authored trigger. It carries no value.");
        return true;
    }

    if (g_lane < 0 || static_cast<std::size_t>(g_lane) >= kLaneLabels.size()) {
        g_lane = 0;
    }

    ImGui::SetNextItemWidth(scaling::pixels(230.0F));
    (void)ImGui::Combo(
        "Channel", &g_lane, kLaneLabels.data(), static_cast<int>(kLaneLabels.size()));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", kLaneHelp[static_cast<std::size_t>(g_lane)]);
    }
    ImGui::SetNextItemWidth(scaling::pixels(230.0F));
    (void)ImGui::SliderFloat("Normalized value", &g_value, 0.0F, 1.0F, "%.2f");
    ImGui::Checkbox("Snap immediately", &g_snap);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Jump to the value with no motion.");
    }

    const auto channel = static_cast<auth::Type23Channel>(g_lane);
    const devices::Status available =
        mapped ? devices::availability(view, generatedSlotRow, channel, g_value, g_snap)
               : devices::Status::invalidSlot;
    const bool ready = mapped && available == devices::Status::ready;
    const bool disabled = instance.outputPending || !ready;
    ImGui::BeginDisabled(disabled);
    if (ImGui::Button("Set channel")) {
        g_result = devices::set_channel(view, generatedSlotRow, channel, g_value, g_snap);
        g_hasResult = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (instance.outputPending) {
        ImGui::TextDisabled("another action is queued");
    } else if (!mapped) {
        ImGui::TextDisabled("%s", status);
    } else if (!ready) {
        ImGui::TextDisabled("%s", devices::status_name(available));
    } else if (g_hasResult) {
        ImGui::TextDisabled("%s", devices::status_name(g_result));
    } else {
        ImGui::TextDisabled("ready");
    }
    return true;
}

} // namespace sunrise::server::ui::activity_host::device_probe
