#pragma once

#include <cstdint>

struct ImGuiTextFilter;

namespace sunrise::state::build_data::scriptables {
struct Snapshot;
}

namespace sunrise::server::ui::activity_host::package_tag_names {

/** Draws one selected package tag name or a compact hexadecimal fallback with hover evidence. */
void draw(const state::build_data::scriptables::Snapshot& snapshot,
          std::uint32_t nameRow,
          std::uint32_t tag) noexcept;

/** @return True when any retained candidate for one package tag passes a text filter. */
[[nodiscard]] bool matches(const state::build_data::scriptables::Snapshot& snapshot,
                           std::uint32_t nameRow,
                           const ImGuiTextFilter& filter) noexcept;

} // namespace sunrise::server::ui::activity_host::package_tag_names
