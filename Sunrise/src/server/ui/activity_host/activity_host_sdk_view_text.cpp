// Display, search and row-match helpers for the generated activity SDK panel.
// Formatting and predicates only: these hold no panel selection and take no lock.

#include "activity_host_sdk_view_text.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <span>
#include <string_view>

#include "../../../state/activity_sdk/runtime.h"

namespace sunrise::server::ui::activity_host::sdk_view {

namespace format = state::activity_sdk::format;
namespace sdk = state::activity_sdk;
/** @return A catalog row count bounded for Dear ImGui's signed clipper. */
[[nodiscard]] int clipped_count(std::size_t count) noexcept {
    return static_cast<int>((std::min)(count, static_cast<std::size_t>(INT_MAX)));
}

/** @return Non-null display storage for one optional generated string. */
[[nodiscard]] const char* display_data(std::string_view value) noexcept {
    return value.empty() ? "-" : value.data();
}

/** @return A generated string length bounded for printf-style presentation. */
[[nodiscard]] int display_length(std::string_view value) noexcept {
    return value.empty() ? 1 : clipped_count(value.size());
}

/** Draws a generated string or a stable missing marker. */
void draw_string(const sdk::Catalog& catalog, format::StringRef reference) noexcept {
    const std::string_view value = catalog.string(reference);
    if (value.empty()) {
        ImGui::TextDisabled("-");
        return;
    }
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}
/** Draws one generated string inline after a fixed label. */
void draw_labeled_string(const char* label,
                         const sdk::Catalog& catalog,
                         format::StringRef reference) noexcept {
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    draw_string(catalog, reference);
}

/** Formats one SHA-256 identity without allocating panel state. */
void draw_digest(const char* label, std::span<const std::byte> digest) noexcept {
    // Lowercase hex alphabet; the panel prints digests in the same form the catalog stores.
    constexpr char digits[] = "0123456789abcdef";
    std::array<char, 65> text{};
    if (digest.size() != 32) {
        ImGui::Text("%s  invalid", label);
        return;
    }
    for (std::size_t index = 0; index < digest.size(); ++index) {
        const unsigned value = std::to_integer<unsigned>(digest[index]);
        text[index * 2] = digits[(value >> 4U) & 0xFU];
        text[index * 2 + 1] = digits[value & 0xFU];
    }
    ImGui::Text("%s  %s", label, text.data());
}

/** Formats the stable inspect, panel-test, and script exposure mask. */
void format_exposures(std::uint32_t flags, std::array<char, 96>& output) noexcept {
    if (flags == 0) {
        (void)std::snprintf(output.data(), output.size(), "none");
        return;
    }
    const std::uint32_t unknown = flags & ~format::kExposureMask;
    (void)std::snprintf(output.data(),
                        output.size(),
                        "%s%s%s%s0x%X",
                        (flags & format::kInspectExposure) != 0 ? "inspect " : "",
                        (flags & format::kPanelTestExposure) != 0 ? "panel-test " : "",
                        (flags & format::kScriptExposure) != 0 ? "script " : "",
                        unknown != 0 ? "unknown " : "mask ",
                        static_cast<unsigned>(flags));
}

/** @return Stable text for one generated alias kind. */
[[nodiscard]] const char* text_kind_name(std::uint32_t kind) noexcept {
    switch (static_cast<format::TextKind>(kind)) {
    case format::TextKind::internalAlias:
        return "internal";
    case format::TextKind::displayAlias:
        return "display";
    case format::TextKind::slotAlias:
        return "slot";
    case format::TextKind::refusalReason:
        return "refusal";
    }
    return "unknown";
}

/** @return Stable text for one generated capability subject. */
[[nodiscard]] const char* subject_kind_name(std::uint32_t kind) noexcept {
    switch (static_cast<format::SubjectKind>(kind)) {
    case format::SubjectKind::activity:
        return "activity";
    case format::SubjectKind::slot:
        return "slot";
    case format::SubjectKind::hostApi:
        return "host API";
    }
    return "unknown";
}
/** @return True when one package alias matches the current search. */
[[nodiscard]] bool aliases_match(const sdk::Catalog& catalog,
                                 const SearchQuery& query,
                                 std::span<const format::Text> aliases) noexcept {
    for (const format::Text& alias : aliases) {
        if (query.matches(catalog, alias.value) || query.matches(text_kind_name(alias.kind))) {
            return true;
        }
    }
    return false;
}

/** @return True when one scenario identity or package tag matches. */
[[nodiscard]] bool scenario_matches(const sdk::Catalog& catalog,
                                    const SearchQuery& query,
                                    const format::Scenario& scenario) noexcept {
    return query.matches(catalog, scenario.id) || query.matches(catalog, scenario.name)
           || query.matches("scenario tag", scenario.tag);
}

/** @return True when one bubble ID, name, hash, ordinal, or row identity matches. */
[[nodiscard]] bool bubble_matches(const sdk::Catalog& catalog,
                                  const SearchQuery& query,
                                  const format::Bubble& bubble) noexcept {
    return query.matches(catalog, bubble.id) || query.matches(catalog, bubble.name)
           || query.matches("scenario index", bubble.scenarioIndex)
           || query.matches("bubble ordinal", bubble.bubbleOrdinal)
           || query.matches("bubble hash", bubble.nameHash);
}

/** @return True when one state ID, hash, ordinal, public value, or row identity matches. */
[[nodiscard]] bool state_matches(const sdk::Catalog& catalog,
                                 const SearchQuery& query,
                                 const format::State& state) noexcept {
    return query.matches(catalog, state.id) || query.matches(catalog, state.entryId)
           || query.matches(catalog, state.registryId)
           || query.matches("scenario index", state.scenarioIndex)
           || query.matches("bubble index", state.bubbleIndex)
           || query.matches("state ordinal", state.stateOrdinal)
           || query.matches("map bubble index", state.mapBubbleIndex)
           || query.matches("state hash", state.stateHash)
           || query.matches("public value", state.publicValue)
           || query.matches("state flags", state.flags)
           || query.matches("registry tag", state.registryTag);
}

/** @return True when a bubble, its scenario, or any owned state matches. */
[[nodiscard]] bool topology_bubble_matches(const sdk::Catalog& catalog,
                                           const SearchQuery& query,
                                           const format::Scenario& scenario,
                                           const format::Bubble& bubble) noexcept {
    if (scenario_matches(catalog, query, scenario) || bubble_matches(catalog, query, bubble)) {
        return true;
    }
    for (const format::State& state : sdk::bubble_states(catalog, bubble)) {
        if (state_matches(catalog, query, state)) {
            return true;
        }
    }
    return false;
}

/** @return True when a state or its exact scenario/bubble context matches. */
[[nodiscard]] bool topology_state_matches(const sdk::Catalog& catalog,
                                          const SearchQuery& query,
                                          const format::Scenario& scenario,
                                          const format::State& state) noexcept {
    if (scenario_matches(catalog, query, scenario) || state_matches(catalog, query, state)) {
        return true;
    }
    const auto bubbles = catalog.bubbles();
    return state.bubbleIndex < bubbles.size()
           && bubble_matches(catalog, query, bubbles[state.bubbleIndex]);
}

/** @return True when one exact slot identity, schema, alias, or numeric field matches. */
[[nodiscard]] bool slot_matches(const sdk::Catalog& catalog,
                                const SearchQuery& query,
                                const format::Slot& slot) noexcept {
    return query.matches(catalog, slot.id) || query.matches(catalog, slot.name)
           || query.matches(catalog, slot.senseSchemaId)
           || query.matches(catalog, slot.authSchemaId)
           || query.matches("object index", slot.objectIndex)
           || query.matches("slot index", slot.slotIndex)
           || query.matches("slot type", slot.slotType)
           || query.matches("component class", slot.componentClass)
           || query.matches("sense schema", slot.senseSchema)
           || query.matches("auth schema", slot.authSchema)
           || query.matches("slot flags", slot.flags)
           || aliases_match(catalog, query, sdk::slot_aliases(catalog, slot));
}

/** @return True when one generated object identity, tag, key, or count matches. */
[[nodiscard]] bool object_matches(const sdk::Catalog& catalog,
                                  const SearchQuery& query,
                                  const format::Object& object) noexcept {
    return query.matches(catalog, object.id) || query.matches("object tag", object.objectTag)
           || query.matches("object key", object.objectKey)
           || query.matches("config count", object.configCount)
           || query.matches("descriptor count", object.descriptorCount)
           || query.matches("placed subblock count", object.placedSubblockCount)
           || query.matches("placed leaf count", object.placedLeafCount)
           || query.matches("placed hop count", object.placedHopCount)
           || query.matches("bare target count", object.bareTargetCount);
}

/** @return True when one occurrence's own registry and topology context matches. */
[[nodiscard]] bool occurrence_identity_matches(const sdk::Catalog& catalog,
                                               const SearchQuery& query,
                                               const format::Occurrence& occurrence) noexcept {
    return query.matches(catalog, occurrence.id)
           || query.matches(catalog, occurrence.contextRegistryKey)
           || query.matches(catalog, occurrence.registryId)
           || query.matches(catalog, occurrence.entryId)
           || query.matches("scenario index", occurrence.scenarioIndex)
           || query.matches("bubble index", occurrence.bubbleIndex)
           || query.matches("state index", occurrence.stateIndex)
           || query.matches("object index", occurrence.objectIndex)
           || query.matches("registry field", occurrence.registryField)
           || query.matches("object ordinal", occurrence.objectOrdinal)
           || query.matches(row_id(catalog, catalog.bubbles(), occurrence.bubbleIndex))
           || query.matches(row_id(catalog, catalog.states(), occurrence.stateIndex));
}

/** @return True when an occurrence, its object, or any reusable slot matches. */
[[nodiscard]] bool occurrence_matches(const sdk::Catalog& catalog,
                                      const SearchQuery& query,
                                      const format::Occurrence& occurrence) noexcept {
    if (query.empty() || occurrence_identity_matches(catalog, query, occurrence)) {
        return true;
    }
    const auto objects = catalog.objects();
    if (occurrence.objectIndex >= objects.size()) {
        return false;
    }
    const format::Object& object = objects[occurrence.objectIndex];
    if (object_matches(catalog, query, object)) {
        return true;
    }
    for (const format::Slot& slot : sdk::object_slots(catalog, object)) {
        if (slot_matches(catalog, query, slot)) {
            return true;
        }
    }
    return false;
}

/** @return True when a selected-object slot or its occurrence/object context matches. */
[[nodiscard]] bool selected_slot_matches(const sdk::Catalog& catalog,
                                         const SearchQuery& query,
                                         const format::Occurrence& occurrence,
                                         const format::Object& object,
                                         const format::Slot& slot) noexcept {
    return query.empty() || occurrence_identity_matches(catalog, query, occurrence)
           || object_matches(catalog, query, object) || slot_matches(catalog, query, slot);
}

/** @return True when any evidence string on one capability gate matches. */
[[nodiscard]] bool gate_matches(const sdk::Catalog& catalog,
                                const SearchQuery& query,
                                const format::Gate& gate) noexcept {
    return query.matches(catalog, gate.gate) || query.matches(catalog, gate.status)
           || query.matches(catalog, gate.reasonCode) || query.matches(catalog, gate.required)
           || query.matches(catalog, gate.observed) || query.matches(catalog, gate.wouldConfirm);
}

/** @return True when one refusal identity, exposure, status, or reason matches. */
[[nodiscard]] bool refusal_matches(const sdk::Catalog& catalog,
                                   const SearchQuery& query,
                                   const format::Refusal& refusal) noexcept {
    return query.matches(catalog, refusal.id) || query.matches(catalog, refusal.exposure)
           || query.matches(catalog, refusal.status)
           || aliases_match(catalog, query, sdk::refusal_reason_codes(catalog, refusal));
}

/** @return True when one capability or any gate/refusal evidence beneath it matches. */
[[nodiscard]] bool capability_matches(const sdk::Catalog& catalog,
                                      const SearchQuery& query,
                                      const format::Capability& capability) noexcept {
    if (query.empty() || query.matches(catalog, capability.id)
        || query.matches(catalog, capability.operation)
        || query.matches(catalog, capability.valueSchemaId)
        || query.matches(subject_kind_name(capability.subjectKind))
        || query.matches("subject kind", capability.subjectKind)
        || query.matches("subject index", capability.subjectIndex)
        || query.matches("exposure flags", capability.exposureFlags)
        || query.matches("candidate exposure flags", capability.candidateExposureFlags)) {
        return true;
    }
    std::array<char, 96> exposure{};
    std::array<char, 96> candidates{};
    format_exposures(capability.exposureFlags, exposure);
    format_exposures(capability.candidateExposureFlags, candidates);
    std::array<char, 128> labeledExposure{};
    std::array<char, 128> labeledCandidates{};
    (void)std::snprintf(
        labeledExposure.data(), labeledExposure.size(), "exposure %s", exposure.data());
    (void)std::snprintf(labeledCandidates.data(),
                        labeledCandidates.size(),
                        "candidate exposure %s",
                        candidates.data());
    if (query.matches(labeledExposure.data()) || query.matches(labeledCandidates.data())) {
        return true;
    }
    for (const format::Gate& gate : sdk::capability_gates(catalog, capability)) {
        if (gate_matches(catalog, query, gate)) {
            return true;
        }
    }
    for (const format::Refusal& refusal : sdk::capability_refusals(catalog, capability)) {
        if (refusal_matches(catalog, query, refusal)) {
            return true;
        }
    }
    return false;
}

} // namespace sunrise::server::ui::activity_host::sdk_view
