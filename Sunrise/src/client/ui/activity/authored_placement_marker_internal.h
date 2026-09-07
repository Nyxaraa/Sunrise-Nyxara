#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "../../../state/activity_sdk/runtime.h"
#include "authored_placement_marker.h"

namespace sunrise::client::ui::activity::authored_placement_marker {

namespace sdk = state::activity_sdk;

/** Immutable drawable-row set published by one browser page. */
struct PublishedRows final {
    Context context{};
    std::vector<Anchor> anchors{};
    PublishedSource source{PublishedSource::authoredAndContainer};
};

/** The page selection and this frame's visibility, read together under one lock. */
struct PageVisibility final {
    WorldPage page{WorldPage::none};
    bool visible{};
};

/** @return The rows the active browser page published, or null when it published none. */
[[nodiscard]] std::shared_ptr<const PublishedRows> published_rows() noexcept;

/** Replaces the published rows. A null value drops them. */
void set_published_rows(std::shared_ptr<const PublishedRows> rows) noexcept;

/** @return The selected page and whether a marker-owning page drew in this ImGui frame. */
[[nodiscard]] PageVisibility page_visibility(int frame) noexcept;

/** Publishes one bounded render-set result for the workbench status line. */
void set_render_diagnostics(const RenderSet& source) noexcept;

/** @return True when a selection context has every required exact identity. */
[[nodiscard]] bool valid_context(const Context& context) noexcept;

/** @return True when one position anchor may enter the renderer. */
[[nodiscard]] bool valid_anchor(const Anchor& anchor) noexcept;

/** @return True when two anchors name the same exact catalog row. */
[[nodiscard]] bool same_anchor_identity(const Anchor& left, const Anchor& right) noexcept;

/** @return True when one source kind belongs to its explicitly named catalog family. */
[[nodiscard]] bool source_matches_context(const Context& context, AnchorSource source) noexcept;

/** Rebuilds one exact SDK-owned point from its global squad and anchor rows. */
[[nodiscard]] bool build_sdk_squad_anchor(const sdk::Catalog& catalog,
                                          std::uint32_t scenarioRow,
                                          std::uint32_t squadRow,
                                          std::uint32_t anchorRow,
                                          Anchor& output) noexcept;

} // namespace sunrise::client::ui::activity::authored_placement_marker
