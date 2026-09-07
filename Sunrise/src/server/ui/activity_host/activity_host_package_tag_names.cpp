#include "activity_host_package_tag_names.h"

#include <algorithm>
#include <cstddef>
#include <imgui.h>

#include "../../../state/build_data/scriptables/definition.h"
#include "activity_host_scriptable_labels.h"

namespace sunrise::server::ui::activity_host::package_tag_names {
namespace {

namespace catalog = state::build_data::scriptables;
namespace labels = server::ui::activity_host::scriptable_labels;

/** Six candidate rows keep an ambiguous-name tooltip compact. */
constexpr std::size_t kTooltipCandidateLimit = 6;

/** @return One checked package-tag name row, or null for the sentinel and stale rows. */
[[nodiscard]] const catalog::TagName* tag_name_row(const catalog::Snapshot& snapshot,
                                                   std::uint32_t row) noexcept {
    return row < snapshot.tagNames.size() ? &snapshot.tagNames[row] : nullptr;
}

/** @return One uniquely selected package name, or null when evidence remains ambiguous. */
[[nodiscard]] const catalog::NameCandidate*
selected_candidate(const catalog::Snapshot& snapshot, const catalog::TagName* name) noexcept {
    return name != nullptr && name->selectedCandidate < snapshot.nameCandidates.size()
               ? &snapshot.nameCandidates[name->selectedCandidate]
               : nullptr;
}

/** Draws bounded provenance and candidate details for one hovered tag label. */
void draw_tooltip(const catalog::Snapshot& snapshot,
                  const catalog::TagName* name,
                  std::uint32_t tag) noexcept {
    ImGui::BeginTooltip();
    if (name == nullptr) {
        ImGui::Text("tag 0x%08X", static_cast<unsigned>(tag));
        ImGui::TextDisabled("no package name row");
        ImGui::EndTooltip();
        return;
    }
    ImGui::Text("tag 0x%08X  class 0x%08X",
                static_cast<unsigned>(name->tag),
                static_cast<unsigned>(name->classId));
    ImGui::Text("%u candidate%s",
                static_cast<unsigned>(name->candidateCount),
                name->candidateCount == 1 ? "" : "s");
    const catalog::NameCandidate* const selected = selected_candidate(snapshot, name);
    if (selected != nullptr) {
        ImGui::Text("selected from %s", labels::provenance(name->provenance));
    } else {
        ImGui::TextDisabled("no unique selected package name");
    }
    const std::size_t first = name->firstCandidate;
    const std::size_t end =
        (std::min)(first + name->candidateCount, snapshot.nameCandidates.size());
    const std::size_t shownEnd = (std::min)(end, first + kTooltipCandidateLimit);
    for (std::size_t index = first; index < shownEnd; ++index) {
        const catalog::NameCandidate& candidate = snapshot.nameCandidates[index];
        ImGui::Text("%.*s", static_cast<int>(candidate.length), candidate.value.data());
        ImGui::SameLine();
        ImGui::TextDisabled("[%s; source 0x%08X/0x%08X]",
                            labels::provenance(candidate.provenance),
                            static_cast<unsigned>(candidate.sourceTag),
                            static_cast<unsigned>(candidate.sourceClassId));
    }
    if (shownEnd < end) {
        ImGui::TextDisabled("+%zu more candidates", end - shownEnd);
    }
    ImGui::EndTooltip();
}

} // namespace

/** Draws one selected package tag name or a short unresolved label. */
void draw(const catalog::Snapshot& snapshot, std::uint32_t nameRow, std::uint32_t tag) noexcept {
    const catalog::TagName* const name = tag_name_row(snapshot, nameRow);
    const catalog::NameCandidate* const selected = selected_candidate(snapshot, name);
    if (selected != nullptr) {
        ImGui::Text("%.*s", static_cast<int>(selected->length), selected->value.data());
    } else {
        ImGui::TextDisabled("Unnamed");
    }
    if (ImGui::IsItemHovered()) {
        draw_tooltip(snapshot, name, tag);
    }
}

/** @return True when any retained candidate for one package tag passes a text filter. */
bool matches(const catalog::Snapshot& snapshot,
             std::uint32_t nameRow,
             const ImGuiTextFilter& filter) noexcept {
    if (!filter.IsActive()) {
        return true;
    }
    const catalog::TagName* const name = tag_name_row(snapshot, nameRow);
    if (name == nullptr) {
        return false;
    }
    const std::size_t first = name->firstCandidate;
    const std::size_t end =
        (std::min)(first + name->candidateCount, snapshot.nameCandidates.size());
    for (std::size_t index = first; index < end; ++index) {
        const catalog::NameCandidate& candidate = snapshot.nameCandidates[index];
        if (filter.PassFilter(candidate.value.data(), candidate.value.data() + candidate.length)) {
            return true;
        }
    }
    return false;
}

} // namespace sunrise::server::ui::activity_host::package_tag_names
