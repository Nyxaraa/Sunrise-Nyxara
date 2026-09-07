// Bound-scenario symbol pages: identity, topology, occurrences, slots and capability evidence.
// The selection globals here are touched only from the render thread, so they take no lock.

#include "activity_host_sdk_symbol_pages.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "../../../middleware/bap/activity_message/scriptable_auth_body.h"
#include "../../../state/activity_sdk/runtime.h"
#include "../../activity/activity_sdk_device_runtime.h"
#include "activity_host_sdk_view_text.h"
#include "activity_host_table_layout.h"

namespace sunrise::server::ui::activity_host::sdk_view {

namespace devices = server::activity::activity_sdk_devices;
namespace format = state::activity_sdk::format;
namespace scriptable_auth = middleware::bap::activity_message::scriptable_auth;
namespace sdk = state::activity_sdk;

namespace {
/** One shared table style, so every table on this page reads the same. */
constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                        | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
/** The page style plus horizontal scroll and resize, for a table wider than the pane. */
constexpr ImGuiTableFlags kWideTableFlags =
    kTableFlags | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable;

std::weak_ptr<const sdk::Catalog> g_selectionCatalog{};
state::activity::SessionBinding g_selectionBinding{};
std::uint64_t g_selectionClientGeneration{};
std::uint32_t g_selectedOccurrence{format::kAbsentIndex};
std::uint32_t g_selectedSlot{format::kAbsentIndex};
std::uint32_t g_deviceSlot{format::kAbsentIndex};
int g_deviceChannel{};
float g_deviceValue{1.0F};
bool g_deviceSnap{};
bool g_hasDeviceResult{};
devices::Status g_deviceResult{devices::Status::invalidSlot};
std::uint32_t g_triggerSlot{format::kAbsentIndex};
bool g_hasTriggerResult{};
devices::Status g_triggerResult{devices::Status::invalidSlot};
std::array<char, 256> g_generatedSymbolSearch{};
std::array<char, 256> g_topologySearch{};
std::vector<std::uint32_t> g_filteredRows{};

/** Draws the selected activity's names first, and its pinned identities on demand. */
void draw_identity(const sdk::BoundView& view,
                   const format::Activity& activity,
                   const format::Scenario& scenario) noexcept {
    const sdk::Catalog& catalog = *view.catalog;
    draw_labeled_string("Activity", catalog, activity.displayName);
    draw_labeled_string("Package", catalog, activity.internalName);
    draw_labeled_string("Scenario", catalog, scenario.name);
    if (!ImGui::TreeNodeEx("Technical details##activity_identity",
                           ImGuiTreeNodeFlags_SpanAvailWidth)) {
        return;
    }
    draw_labeled_string("Activity ID", catalog, activity.id);
    draw_labeled_string("Scenario ID", catalog, scenario.id);
    ImGui::Text("activity row %u  index %u  definition 0x%08X  flags 0x%08X",
                static_cast<unsigned>(view.activityRow),
                static_cast<unsigned>(activity.activityIndex),
                static_cast<unsigned>(activity.definitionHash),
                static_cast<unsigned>(activity.flags));
    ImGui::Text("scenario row %u  tag 0x%08X",
                static_cast<unsigned>(view.scenarioRow),
                static_cast<unsigned>(scenario.tag));
    ImGui::Text("session 0x%llX  revision %llu  ActivityClient generation %llu",
                static_cast<unsigned long long>(view.binding.sessionId),
                static_cast<unsigned long long>(view.binding.createdRevision),
                static_cast<unsigned long long>(view.activityClientGeneration));
    draw_digest("SDK build", catalog.sdk_build_sha256());
    draw_digest("Content key", catalog.content_key_sha256());
    draw_digest("Logical IR", catalog.logical_ir_sha256());
    ImGui::TreePop();
}

/** Draws generated internal and display aliases. */
void draw_aliases(const sdk::Catalog& catalog,
                  std::span<const format::Text> aliases,
                  const char* tableId) noexcept {
    if (aliases.empty()) {
        ImGui::TextDisabled("No aliases");
        return;
    }
    if (!ImGui::BeginTable(tableId, 2, kTableFlags, table_layout::size(aliases.size()))) {
        return;
    }
    ImGui::TableSetupColumn("kind");
    ImGui::TableSetupColumn("alias", ImGuiTableColumnFlags_WidthStretch);
    table_layout::frozen_headers();
    for (const format::Text& alias : aliases) {
        table_layout::next_row();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(text_kind_name(alias.kind));
        ImGui::TableNextColumn();
        draw_string(catalog, alias.value);
    }
    ImGui::EndTable();
}

/** Draws the bubbles and their generated state slices. */
void draw_bubbles(const sdk::Catalog& catalog,
                  const format::Scenario& scenario,
                  const SearchQuery& query) noexcept {
    const auto bubbles = sdk::scenario_bubbles(catalog, scenario);
    g_filteredRows.clear();
    g_filteredRows.reserve(bubbles.size());
    for (std::size_t index = 0; index < bubbles.size(); ++index) {
        if (topology_bubble_matches(catalog, query, scenario, bubbles[index])) {
            g_filteredRows.push_back(static_cast<std::uint32_t>(index));
        }
    }
    ImGui::Text("%zu of %zu bubbles", g_filteredRows.size(), bubbles.size());
    if (!ImGui::BeginTable(
            "##sdk_bubbles", 5, kTableFlags, table_layout::size(g_filteredRows.size()))) {
        return;
    }
    ImGui::TableSetupColumn("ordinal");
    ImGui::TableSetupColumn("hash");
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("states");
    table_layout::frozen_headers();
    for (const std::uint32_t index : g_filteredRows) {
        const format::Bubble& bubble = bubbles[index];
        table_layout::next_row();
        ImGui::TableNextColumn();
        ImGui::Text("%u", static_cast<unsigned>(bubble.bubbleOrdinal));
        ImGui::TableNextColumn();
        ImGui::Text("0x%08X", static_cast<unsigned>(bubble.nameHash));
        ImGui::TableNextColumn();
        draw_string(catalog, bubble.name);
        ImGui::TableNextColumn();
        draw_string(catalog, bubble.id);
        ImGui::TableNextColumn();
        ImGui::Text("%zu", sdk::bubble_states(catalog, bubble).size());
    }
    ImGui::EndTable();
}

/** Draws all exact state identities in the scenario. */
void draw_states(const sdk::Catalog& catalog,
                 const format::Scenario& scenario,
                 const SearchQuery& query) noexcept {
    const auto states = sdk::scenario_states(catalog, scenario);
    g_filteredRows.clear();
    g_filteredRows.reserve(states.size());
    for (std::size_t index = 0; index < states.size(); ++index) {
        if (topology_state_matches(catalog, query, scenario, states[index])) {
            g_filteredRows.push_back(static_cast<std::uint32_t>(index));
        }
    }
    ImGui::Text("%zu of %zu states", g_filteredRows.size(), states.size());
    if (!ImGui::BeginTable(
            "##sdk_states", 9, kWideTableFlags, table_layout::size(g_filteredRows.size()))) {
        return;
    }
    ImGui::TableSetupColumn("ordinal");
    ImGui::TableSetupColumn("bubble row");
    ImGui::TableSetupColumn("hash");
    ImGui::TableSetupColumn("public");
    ImGui::TableSetupColumn("flags");
    ImGui::TableSetupColumn("map bubble");
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("entry ID");
    ImGui::TableSetupColumn("registry ID");
    table_layout::frozen_headers();
    ImGuiListClipper clipper;
    clipper.Begin(clipped_count(g_filteredRows.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t index = g_filteredRows[static_cast<std::size_t>(row)];
            const format::State& state = states[index];
            table_layout::next_row();
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(state.stateOrdinal));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(state.bubbleIndex));
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", static_cast<unsigned>(state.stateHash));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(state.publicValue));
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", static_cast<unsigned>(state.flags));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(state.mapBubbleIndex));
            ImGui::TableNextColumn();
            draw_string(catalog, state.id);
            ImGui::TableNextColumn();
            draw_string(catalog, state.entryId);
            ImGui::TableNextColumn();
            draw_string(catalog, state.registryId);
        }
    }
    ImGui::EndTable();
}

/** Draws the scenario occurrence symbols and updates the selected object. */
void draw_occurrences(const sdk::Catalog& catalog,
                      std::span<const format::Occurrence> occurrences,
                      const SearchQuery& query) noexcept {
    const auto objects = catalog.objects();
    const auto bubbles = catalog.bubbles();
    const auto states = catalog.states();
    g_filteredRows.clear();
    g_filteredRows.reserve(occurrences.size());
    for (std::size_t index = 0; index < occurrences.size(); ++index) {
        if (occurrence_matches(catalog, query, occurrences[index])) {
            g_filteredRows.push_back(static_cast<std::uint32_t>(index));
        }
    }
    ImGui::Text("%zu of %zu objects", g_filteredRows.size(), occurrences.size());
    if (!ImGui::BeginTable(
            "##sdk_occurrences", 8, kWideTableFlags, table_layout::size(g_filteredRows.size()))) {
        return;
    }
    ImGui::TableSetupColumn("occurrence", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("object", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("context registry", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("registry ID");
    ImGui::TableSetupColumn("entry ID");
    ImGui::TableSetupColumn("field");
    ImGui::TableSetupColumn("bubble");
    ImGui::TableSetupColumn("state");
    table_layout::frozen_headers();
    ImGuiListClipper clipper;
    clipper.Begin(clipped_count(g_filteredRows.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t index = g_filteredRows[static_cast<std::size_t>(row)];
            const format::Occurrence& occurrence = occurrences[index];
            table_layout::next_row();
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(index));
            const std::string_view id = catalog.string(occurrence.id);
            std::array<char, 160> label{};
            (void)std::snprintf(
                label.data(), label.size(), "%.*s", display_length(id), display_data(id));
            if (table_layout::selectable(
                    label.data(), g_selectedOccurrence == static_cast<std::uint32_t>(index))) {
                g_selectedOccurrence = static_cast<std::uint32_t>(index);
                g_selectedSlot = format::kAbsentIndex;
                g_deviceSlot = format::kAbsentIndex;
                g_hasDeviceResult = false;
            }
            ImGui::TableNextColumn();
            const std::string_view objectId = row_id(catalog, objects, occurrence.objectIndex);
            if (objectId.empty()) {
                ImGui::TextDisabled("invalid row %u",
                                    static_cast<unsigned>(occurrence.objectIndex));
            } else {
                ImGui::TextUnformatted(objectId.data(), objectId.data() + objectId.size());
            }
            ImGui::TableNextColumn();
            draw_string(catalog, occurrence.contextRegistryKey);
            ImGui::TableNextColumn();
            draw_string(catalog, occurrence.registryId);
            ImGui::TableNextColumn();
            draw_string(catalog, occurrence.entryId);
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(occurrence.registryField));
            ImGui::TableNextColumn();
            const std::string_view bubbleId = row_id(catalog, bubbles, occurrence.bubbleIndex);
            if (bubbleId.empty()) {
                ImGui::Text("row %u", static_cast<unsigned>(occurrence.bubbleIndex));
            } else {
                ImGui::TextUnformatted(bubbleId.data(), bubbleId.data() + bubbleId.size());
            }
            ImGui::TableNextColumn();
            const std::string_view stateId = row_id(catalog, states, occurrence.stateIndex);
            if (stateId.empty()) {
                ImGui::Text("row %u", static_cast<unsigned>(occurrence.stateIndex));
            } else {
                ImGui::TextUnformatted(stateId.data(), stateId.data() + stateId.size());
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

/** Draws package aliases attached to one object slot. */
void draw_slot_aliases(const sdk::Catalog& catalog, const format::Slot& slot) noexcept {
    const auto aliases = sdk::slot_aliases(catalog, slot);
    ImGui::Text("%zu alias%s", aliases.size(), aliases.size() == 1 ? "" : "es");
    for (const format::Text& alias : aliases) {
        ImGui::Bullet();
        ImGui::SameLine();
        draw_string(catalog, alias.value);
    }
}

/** Draws the exact generated SDK type-23 control used by Lua `SlotView:set_channel`. */
void draw_device_action(const sdk::BoundView& view,
                        const format::Slot& slot,
                        std::uint32_t slotRow) noexcept {
    if (slot.slotType != format::kDeviceSlotType
        || slot.componentClass != format::kDeviceComponentClass
        || slot.senseSchema != format::kDeviceSenseSchema
        || slot.authSchema != format::kDeviceAuthSchema
        || (slot.flags & format::kSlotSchemaJoinExact) == 0) {
        return;
    }
    if (g_deviceSlot != slotRow) {
        g_deviceSlot = slotRow;
        g_deviceChannel = 0;
        g_deviceValue = 1.0F;
        g_deviceSnap = false;
        g_hasDeviceResult = false;
    }
    // Channel names in type-23 channel order; the index is the channel the action sets.
    constexpr std::array<const char*, scriptable_auth::kType23ChannelCount> kChannels{
        "Position", "Power", "Lock"};
    if (g_deviceChannel < 0 || static_cast<std::size_t>(g_deviceChannel) >= kChannels.size()) {
        g_deviceChannel = 0;
    }
    ImGui::SeparatorText("Set channel");
    ImGui::PushID("sdk_device_action");
    ImGui::SetNextItemWidth(230.0F);
    (void)ImGui::Combo(
        "Channel", &g_deviceChannel, kChannels.data(), static_cast<int>(kChannels.size()));
    ImGui::SetNextItemWidth(230.0F);
    (void)ImGui::SliderFloat("Normalized value", &g_deviceValue, 0.0F, 1.0F, "%.2f");
    ImGui::Checkbox("Snap immediately", &g_deviceSnap);
    const auto channel = static_cast<scriptable_auth::Type23Channel>(g_deviceChannel);
    const devices::Status available =
        devices::availability(view, slotRow, channel, g_deviceValue, g_deviceSnap);
    ImGui::BeginDisabled(available != devices::Status::ready);
    if (ImGui::Button("Set channel")) {
        g_deviceResult = devices::set_channel(view, slotRow, channel, g_deviceValue, g_deviceSnap);
        g_hasDeviceResult = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("availability: %s", devices::status_name(available));
    if (g_hasDeviceResult) {
        ImGui::Text("Set result  %s", devices::status_name(g_deviceResult));
    }
    ImGui::TextDisabled("0 to 1. Each end is authored per device.");
    ImGui::PopID();
}

/** Draws the exact generated SDK type-31 action used by Lua `SlotView:fire_trigger`. */
void draw_trigger_action(const sdk::BoundView& view,
                         const format::Slot& slot,
                         std::uint32_t slotRow) noexcept {
    if (slot.slotType != scriptable_auth::kType31SlotType
        || slot.authSchema != scriptable_auth::kType31Schema
        || (slot.flags & format::kSlotSchemaJoinExact) == 0) {
        return;
    }
    if (g_triggerSlot != slotRow) {
        g_triggerSlot = slotRow;
        g_hasTriggerResult = false;
    }
    ImGui::SeparatorText("Fire trigger");
    ImGui::PushID("sdk_trigger_action");
    const devices::Status available = devices::trigger_availability(view, slotRow);
    ImGui::BeginDisabled(available != devices::Status::ready);
    if (ImGui::Button("Fire trigger")) {
        g_triggerResult = devices::fire_trigger(view, slotRow);
        g_hasTriggerResult = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("availability: %s", devices::status_name(available));
    if (g_hasTriggerResult) {
        ImGui::Text("Fire result  %s", devices::status_name(g_triggerResult));
    }
    ImGui::PopID();
}

/** Draws the selected object's exact reusable slot symbols. */
void draw_slots(const sdk::Catalog& catalog,
                const format::Occurrence& occurrence,
                const format::Object& object,
                const SearchQuery& query) noexcept {
    const auto slots = sdk::object_slots(catalog, object);
    if (g_selectedSlot >= slots.size()) {
        g_selectedSlot = slots.empty() ? format::kAbsentIndex : 0;
    }
    g_filteredRows.clear();
    g_filteredRows.reserve(slots.size());
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (selected_slot_matches(catalog, query, occurrence, object, slots[index])) {
            g_filteredRows.push_back(static_cast<std::uint32_t>(index));
        }
    }
    ImGui::Text("%zu of %zu slots", g_filteredRows.size(), slots.size());
    if (!ImGui::BeginTable(
            "##sdk_slots", 8, kWideTableFlags, table_layout::size(g_filteredRows.size()))) {
        return;
    }
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("index");
    ImGui::TableSetupColumn("type");
    ImGui::TableSetupColumn("class");
    ImGui::TableSetupColumn("sense schema");
    ImGui::TableSetupColumn("auth schema");
    ImGui::TableSetupColumn("flags");
    table_layout::frozen_headers();
    ImGuiListClipper clipper;
    clipper.Begin(clipped_count(g_filteredRows.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const std::size_t index = g_filteredRows[static_cast<std::size_t>(row)];
            const format::Slot& slot = slots[index];
            table_layout::next_row();
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(index));
            const std::string_view name = catalog.string(slot.name);
            std::array<char, 160> label{};
            (void)std::snprintf(
                label.data(), label.size(), "%.*s", display_length(name), display_data(name));
            if (table_layout::selectable(label.data(),
                                         g_selectedSlot == static_cast<std::uint32_t>(index))) {
                g_selectedSlot = static_cast<std::uint32_t>(index);
            }
            ImGui::TableNextColumn();
            draw_string(catalog, slot.id);
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(slot.slotIndex));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(slot.slotType));
            ImGui::TableNextColumn();
            ImGui::Text("%u", static_cast<unsigned>(slot.componentClass));
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", static_cast<unsigned>(slot.senseSchema));
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", static_cast<unsigned>(slot.authSchema));
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", static_cast<unsigned>(slot.flags));
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

/** Draws all evidence gates and their generated refusal reason codes. */
void draw_capability_evidence(const sdk::Catalog& catalog,
                              const format::Capability& capability) noexcept {
    const auto gates = sdk::capability_gates(catalog, capability);
    if (gates.empty()) {
        ImGui::TextDisabled("No gates");
    } else if (ImGui::BeginTable(
                   "##sdk_gates", 6, kWideTableFlags, table_layout::size(gates.size()))) {
        ImGui::TableSetupColumn("gate");
        ImGui::TableSetupColumn("status");
        ImGui::TableSetupColumn("reason code", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("required", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("observed", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("would confirm", ImGuiTableColumnFlags_WidthStretch);
        table_layout::frozen_headers();
        for (const format::Gate& gate : gates) {
            table_layout::next_row();
            ImGui::TableNextColumn();
            draw_string(catalog, gate.gate);
            ImGui::TableNextColumn();
            draw_string(catalog, gate.status);
            ImGui::TableNextColumn();
            draw_string(catalog, gate.reasonCode);
            ImGui::TableNextColumn();
            draw_string(catalog, gate.required);
            ImGui::TableNextColumn();
            draw_string(catalog, gate.observed);
            ImGui::TableNextColumn();
            draw_string(catalog, gate.wouldConfirm);
        }
        ImGui::EndTable();
    }

    const auto refusals = sdk::capability_refusals(catalog, capability);
    if (refusals.empty()) {
        ImGui::TextDisabled("No exposure refusals");
        return;
    }
    ImGui::Text("%zu exposure refusal%s", refusals.size(), refusals.size() == 1 ? "" : "s");
    for (std::size_t index = 0; index < refusals.size(); ++index) {
        const format::Refusal& refusal = refusals[index];
        const std::string_view exposure = catalog.string(refusal.exposure);
        const std::string_view status = catalog.string(refusal.status);
        ImGui::PushID(static_cast<int>(index));
        const bool expanded = ImGui::TreeNodeEx("##sdk_refusal",
                                                ImGuiTreeNodeFlags_SpanAvailWidth,
                                                "%.*s: %.*s",
                                                display_length(exposure),
                                                display_data(exposure),
                                                display_length(status),
                                                display_data(status));
        if (expanded) {
            draw_labeled_string("Refusal ID", catalog, refusal.id);
            const auto reasons = sdk::refusal_reason_codes(catalog, refusal);
            for (const format::Text& reason : reasons) {
                ImGui::Bullet();
                ImGui::SameLine();
                draw_string(catalog, reason.value);
            }
            if (reasons.empty()) {
                ImGui::TextDisabled("No reason codes");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

/** Draws candidate operation, exposure, gate, and refusal rows. */
void draw_capabilities(const sdk::Catalog& catalog,
                       std::span<const format::Capability> capabilities,
                       const char* emptyText,
                       const SearchQuery& query) noexcept {
    g_filteredRows.clear();
    g_filteredRows.reserve(capabilities.size());
    for (std::size_t index = 0; index < capabilities.size(); ++index) {
        if (capability_matches(catalog, query, capabilities[index])) {
            g_filteredRows.push_back(static_cast<std::uint32_t>(index));
        }
    }
    ImGui::Text("%zu of %zu operations", g_filteredRows.size(), capabilities.size());
    if (capabilities.empty()) {
        ImGui::TextDisabled("%s", emptyText);
        return;
    }
    if (g_filteredRows.empty()) {
        ImGui::TextDisabled("Nothing matches this search");
        return;
    }
    for (const std::uint32_t index : g_filteredRows) {
        const format::Capability& capability = capabilities[index];
        const std::string_view operation = catalog.string(capability.operation);
        std::array<char, 96> exposure{};
        std::array<char, 96> candidates{};
        format_exposures(capability.exposureFlags, exposure);
        format_exposures(capability.candidateExposureFlags, candidates);
        ImGui::PushID(static_cast<int>(index));
        const bool expanded = ImGui::TreeNodeEx("##sdk_capability",
                                                ImGuiTreeNodeFlags_SpanAvailWidth,
                                                "%.*s  candidate: %s",
                                                display_length(operation),
                                                display_data(operation),
                                                candidates.data());
        if (expanded) {
            draw_labeled_string("Capability ID", catalog, capability.id);
            draw_labeled_string("Value schema", catalog, capability.valueSchemaId);
            if (capability.subjectKind
                == static_cast<std::uint32_t>(format::SubjectKind::hostApi)) {
                ImGui::TextUnformatted("Subject host API");
            } else {
                ImGui::Text("Subject %s row %u",
                            subject_kind_name(capability.subjectKind),
                            static_cast<unsigned>(capability.subjectIndex));
            }
            ImGui::Text("Exposure  %s", exposure.data());
            ImGui::Text("Candidate exposure  %s", candidates.data());
            draw_capability_evidence(catalog, capability);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

/** Draws the selected occurrence, object, slot, and operation chain. */
void draw_selected_object(const sdk::BoundView& view,
                          std::span<const format::Occurrence> occurrences,
                          const SearchQuery& query) noexcept {
    const sdk::Catalog& catalog = *view.catalog;
    if (g_selectedOccurrence >= occurrences.size()) {
        ImGui::TextDisabled("No occurrence selected");
        return;
    }
    const format::Occurrence& occurrence = occurrences[g_selectedOccurrence];
    const auto objects = catalog.objects();
    if (occurrence.objectIndex >= objects.size()) {
        ImGui::Text("Object row %u is invalid", static_cast<unsigned>(occurrence.objectIndex));
        return;
    }
    const format::Object& object = objects[occurrence.objectIndex];
    ImGui::SeparatorText("Selected object");
    if (!occurrence_matches(catalog, query, occurrence)) {
        ImGui::TextDisabled("This object is outside the search.");
    }
    draw_labeled_string("Occurrence ID", catalog, occurrence.id);
    draw_labeled_string("Object ID", catalog, object.id);
    ImGui::Text("Object row %u  tag 0x%08X  key 0x%08X",
                static_cast<unsigned>(occurrence.objectIndex),
                static_cast<unsigned>(object.objectTag),
                static_cast<unsigned>(object.objectKey));
    ImGui::Text("config %u  descriptor %u  placed subblock %u  leaf %u  hop %u  bare target %u",
                static_cast<unsigned>(object.configCount),
                static_cast<unsigned>(object.descriptorCount),
                static_cast<unsigned>(object.placedSubblockCount),
                static_cast<unsigned>(object.placedLeafCount),
                static_cast<unsigned>(object.placedHopCount),
                static_cast<unsigned>(object.bareTargetCount));
    draw_slots(catalog, occurrence, object, query);
    const auto slots = sdk::object_slots(catalog, object);
    if (g_selectedSlot >= slots.size()) {
        return;
    }
    const format::Slot& slot = slots[g_selectedSlot];
    if (!selected_slot_matches(catalog, query, occurrence, object, slot)) {
        ImGui::TextDisabled("This slot is outside the search.");
        return;
    }
    ImGui::SeparatorText("Selected slot");
    draw_labeled_string("Slot ID", catalog, slot.id);
    draw_labeled_string("Slot name", catalog, slot.name);
    draw_labeled_string("Sense schema ID", catalog, slot.senseSchemaId);
    draw_labeled_string("Auth schema ID", catalog, slot.authSchemaId);
    draw_slot_aliases(catalog, slot);
    ImGui::TextUnformatted("Operations");
    ImGui::PushID("slot_capabilities");
    draw_capabilities(
        catalog, sdk::slot_capabilities(catalog, slot), "No operations", SearchQuery{});
    ImGui::PopID();
    const auto allSlots = catalog.slots();
    if (&slot >= allSlots.data() && &slot < allSlots.data() + allSlots.size()) {
        const std::uint32_t slotRow = static_cast<std::uint32_t>(&slot - allSlots.data());
        draw_device_action(view, slot, slotRow);
        draw_trigger_action(view, slot, slotRow);
    }
}

} // namespace

/** Keeps object and slot selection pinned to one catalog and client generation. */
void sync_selection(const sdk::BoundView& view,
                    std::span<const format::Occurrence> occurrences) noexcept {
    if (g_selectionCatalog.lock() != view.catalog || !same_binding(g_selectionBinding, view.binding)
        || g_selectionClientGeneration != view.activityClientGeneration) {
        g_selectionCatalog = view.catalog;
        g_selectionBinding = view.binding;
        g_selectionClientGeneration = view.activityClientGeneration;
        g_selectedOccurrence = occurrences.empty() ? format::kAbsentIndex : 0;
        g_selectedSlot = format::kAbsentIndex;
        g_deviceSlot = format::kAbsentIndex;
        g_hasDeviceResult = false;
    }
    if (g_selectedOccurrence >= occurrences.size()) {
        g_selectedOccurrence = occurrences.empty() ? format::kAbsentIndex : 0;
        g_selectedSlot = format::kAbsentIndex;
    }
}

} // namespace sunrise::server::ui::activity_host::sdk_view
