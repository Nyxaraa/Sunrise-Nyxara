#pragma once

#include <cstdint>

// The client registers each alternate scenario entry as its own packed region, bubble times eight
// plus the state ordinal. Two states of one bubble are two worlds, so travel and arrival compare
// the complete packed region.

namespace sunrise::server::bap::encrypted::push::activity {

/** @return True when the client holds the exact region a pending selection names. */
[[nodiscard]] constexpr bool
mission_seed_arrival_window_closed(std::int32_t heldRegion,
                                   std::uint32_t pendingEffectiveRegion) noexcept {
    return heldRegion >= 0 && static_cast<std::uint32_t>(heldRegion) == pendingEffectiveRegion;
}

/** @return True when a selection replaces the world and the client has not reached it yet. */
[[nodiscard]] constexpr bool mission_seed_selection_needs_arrival(
    std::uint32_t oldRegion, std::uint32_t newRegion, std::int32_t heldRegion) noexcept {
    return oldRegion != newRegion && !mission_seed_arrival_window_closed(heldRegion, newRegion);
}

/** @return True while only the transition subset may publish: before the exact arrival. */
[[nodiscard]] constexpr bool
mission_seed_transition_subset_only(bool fullSetPublished,
                                    bool scriptSelected,
                                    bool publicRegion,
                                    std::int32_t heldRegion,
                                    std::uint32_t selectedRegion) noexcept {
    return !fullSetPublished
           && ((!scriptSelected && !publicRegion)
               || !mission_seed_arrival_window_closed(heldRegion, selectedRegion));
}

} // namespace sunrise::server::bap::encrypted::push::activity
