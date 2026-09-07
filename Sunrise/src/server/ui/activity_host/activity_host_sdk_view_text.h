#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <span>
#include <string_view>

#include "../../../state/activity_sdk/runtime.h"

namespace sunrise::server::ui::activity_host::sdk_view {

/** @return A catalog row count bounded for Dear ImGui's signed clipper. */
[[nodiscard]] int clipped_count(std::size_t count) noexcept;

/** @return Non-null display storage for one optional generated string. */
[[nodiscard]] const char* display_data(std::string_view value) noexcept;

/** @return A generated string length bounded for printf-style presentation. */
[[nodiscard]] int display_length(std::string_view value) noexcept;

/** Draws a generated string or a stable missing marker. */
void draw_string(const state::activity_sdk::Catalog& catalog,
                 state::activity_sdk::format::StringRef reference) noexcept;

/** @return A generated string when one global row index is valid. */
template <typename Row>
[[nodiscard]] std::string_view row_id(const state::activity_sdk::Catalog& catalog,
                                      std::span<const Row> rows,
                                      std::uint32_t index) noexcept {
    return index < rows.size() ? catalog.string(rows[index].id) : std::string_view{};
}

/** Draws one generated string inline after a fixed label. */
void draw_labeled_string(const char* label,
                         const state::activity_sdk::Catalog& catalog,
                         state::activity_sdk::format::StringRef reference) noexcept;

/** Formats one SHA-256 identity without allocating panel state. */
void draw_digest(const char* label, std::span<const std::byte> digest) noexcept;

/** Formats the stable inspect, panel-test, and script exposure mask. */
void format_exposures(std::uint32_t flags, std::array<char, 96>& output) noexcept;

/** @return Stable text for one generated alias kind. */
[[nodiscard]] const char* text_kind_name(std::uint32_t kind) noexcept;

/** @return Stable text for one generated capability subject. */
[[nodiscard]] const char* subject_kind_name(std::uint32_t kind) noexcept;

/** @return True for ASCII whitespace accepted around one panel search. */
[[nodiscard]] constexpr bool search_space(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

/** @return One trimmed, null-terminated panel search buffer. */
template <std::size_t Size>
[[nodiscard]] std::string_view search_text(const std::array<char, Size>& buffer) noexcept {
    std::string_view value(buffer.data());
    while (!value.empty() && search_space(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && search_space(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

/** One case-insensitive generated-row search with decimal and hexadecimal numeric matching. */
struct SearchQuery final {
    std::string_view value{};

    /** @return True when the user has not entered a searchable value. */
    [[nodiscard]] bool empty() const noexcept {
        return value.empty();
    }

    /** @return True when one generated string contains this search. */
    [[nodiscard]] bool matches(std::string_view candidate) const noexcept {
        if (empty()) {
            return true;
        }
        if (candidate.size() < value.size()) {
            return false;
        }
        const auto fold = [](char character) noexcept {
            return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A'))
                                                        : character;
        };
        for (std::size_t offset = 0; offset + value.size() <= candidate.size(); ++offset) {
            bool equal = true;
            for (std::size_t index = 0; index < value.size(); ++index) {
                if (fold(candidate[offset + index]) != fold(value[index])) {
                    equal = false;
                    break;
                }
            }
            if (equal) {
                return true;
            }
        }
        return false;
    }

    /** @return True when one generated string reference contains this search. */
    [[nodiscard]] bool matches(const state::activity_sdk::Catalog& catalog,
                               state::activity_sdk::format::StringRef reference) const noexcept {
        return matches(catalog.string(reference));
    }

    /** @return True when decimal, compact hex, padded hex, or its field label matches. */
    [[nodiscard]] bool matches(std::string_view label, std::uint32_t number) const noexcept {
        std::array<char, 96> text{};
        (void)std::snprintf(text.data(),
                            text.size(),
                            "%.*s %u 0x%X 0x%08X",
                            clipped_count(label.size()),
                            label.data(),
                            static_cast<unsigned>(number),
                            static_cast<unsigned>(number),
                            static_cast<unsigned>(number));
        return matches(std::string_view(text.data()));
    }
};

/** Draws one stable search box shared by a generated SDK collection. */
template <std::size_t Size>
void draw_search(std::array<char, Size>& buffer, const char* id, const char* hint) noexcept {
    ImGui::SetNextItemWidth(420.0F);
    (void)ImGui::InputTextWithHint(id, hint, buffer.data(), buffer.size());
}

/** @return True when one package alias matches the current search. */
[[nodiscard]] bool
aliases_match(const state::activity_sdk::Catalog& catalog,
              const SearchQuery& query,
              std::span<const state::activity_sdk::format::Text> aliases) noexcept;

/** @return True when one scenario identity or package tag matches. */
[[nodiscard]] bool scenario_matches(const state::activity_sdk::Catalog& catalog,
                                    const SearchQuery& query,
                                    const state::activity_sdk::format::Scenario& scenario) noexcept;

/** @return True when one bubble ID, name, hash, ordinal, or row identity matches. */
[[nodiscard]] bool bubble_matches(const state::activity_sdk::Catalog& catalog,
                                  const SearchQuery& query,
                                  const state::activity_sdk::format::Bubble& bubble) noexcept;

/** @return True when one state ID, hash, ordinal, public value, or row identity matches. */
[[nodiscard]] bool state_matches(const state::activity_sdk::Catalog& catalog,
                                 const SearchQuery& query,
                                 const state::activity_sdk::format::State& state) noexcept;

/** @return True when a bubble, its scenario, or any owned state matches. */
[[nodiscard]] bool
topology_bubble_matches(const state::activity_sdk::Catalog& catalog,
                        const SearchQuery& query,
                        const state::activity_sdk::format::Scenario& scenario,
                        const state::activity_sdk::format::Bubble& bubble) noexcept;

/** @return True when a state or its exact scenario/bubble context matches. */
[[nodiscard]] bool topology_state_matches(const state::activity_sdk::Catalog& catalog,
                                          const SearchQuery& query,
                                          const state::activity_sdk::format::Scenario& scenario,
                                          const state::activity_sdk::format::State& state) noexcept;

/** @return True when one exact slot identity, schema, alias, or numeric field matches. */
[[nodiscard]] bool slot_matches(const state::activity_sdk::Catalog& catalog,
                                const SearchQuery& query,
                                const state::activity_sdk::format::Slot& slot) noexcept;

/** @return True when one generated object identity, tag, key, or count matches. */
[[nodiscard]] bool object_matches(const state::activity_sdk::Catalog& catalog,
                                  const SearchQuery& query,
                                  const state::activity_sdk::format::Object& object) noexcept;

/** @return True when one occurrence's own registry and topology context matches. */
[[nodiscard]] bool
occurrence_identity_matches(const state::activity_sdk::Catalog& catalog,
                            const SearchQuery& query,
                            const state::activity_sdk::format::Occurrence& occurrence) noexcept;

/** @return True when an occurrence, its object, or any reusable slot matches. */
[[nodiscard]] bool
occurrence_matches(const state::activity_sdk::Catalog& catalog,
                   const SearchQuery& query,
                   const state::activity_sdk::format::Occurrence& occurrence) noexcept;

/** @return True when a selected-object slot or its occurrence/object context matches. */
[[nodiscard]] bool selected_slot_matches(const state::activity_sdk::Catalog& catalog,
                                         const SearchQuery& query,
                                         const state::activity_sdk::format::Occurrence& occurrence,
                                         const state::activity_sdk::format::Object& object,
                                         const state::activity_sdk::format::Slot& slot) noexcept;

/** @return True when any evidence string on one capability gate matches. */
[[nodiscard]] bool gate_matches(const state::activity_sdk::Catalog& catalog,
                                const SearchQuery& query,
                                const state::activity_sdk::format::Gate& gate) noexcept;

/** @return True when one refusal identity, exposure, status, or reason matches. */
[[nodiscard]] bool refusal_matches(const state::activity_sdk::Catalog& catalog,
                                   const SearchQuery& query,
                                   const state::activity_sdk::format::Refusal& refusal) noexcept;

/** @return True when one capability or any gate/refusal evidence beneath it matches. */
[[nodiscard]] bool
capability_matches(const state::activity_sdk::Catalog& catalog,
                   const SearchQuery& query,
                   const state::activity_sdk::format::Capability& capability) noexcept;

} // namespace sunrise::server::ui::activity_host::sdk_view
