#pragma once

#include <cstdint>

namespace sunrise::server::bap::encrypted::push::activity {

/**
 * Native 4C8E60 registers the base entry at bubble*8; 4C8E40 registers each
 * alternate at bubble*8+ordinal. Sharing a bubble does not instantiate its
 * alternate cinematic world. Travel/arrival use the complete packed region.
 */
[[nodiscard]] constexpr bool mission_seed_region_change_replaces_world(
    std::uint32_t /*currentSliceSetIndex*/, std::uint32_t currentEffectiveRegion,
    std::uint32_t /*selectedSliceSetIndex*/, std::uint32_t selectedEffectiveRegion) noexcept {
    return currentEffectiveRegion != selectedEffectiveRegion;
}

/** A pending world is held only after the native client reports that exact variant. */
[[nodiscard]] constexpr bool mission_seed_arrival_window_closed(
    std::int32_t heldRegion, std::uint32_t pendingEffectiveRegion,
    std::uint32_t /*pendingSliceSetIndex*/, std::uint32_t sliceSetFactor) noexcept {
    return heldRegion >= 0 && sliceSetFactor != 0
        && static_cast<std::uint32_t>(heldRegion) == pendingEffectiveRegion;
}

/** Ordinary traversal may already have reached the exact destination of a stale seed plan. */
[[nodiscard]] constexpr bool mission_seed_selection_needs_arrival(
    std::uint32_t oldSliceSet, std::uint32_t oldRegion,
    std::uint32_t newSliceSet, std::uint32_t newRegion,
    std::int32_t heldRegion, std::uint32_t factor) noexcept {
    return mission_seed_region_change_replaces_world(oldSliceSet, oldRegion, newSliceSet, newRegion)
        && !mission_seed_arrival_window_closed(heldRegion, newRegion, newSliceSet, factor);
}

/** Keep state-local records behind actual arrival, including alternate bookend worlds. */
[[nodiscard]] constexpr bool mission_seed_transition_subset_only(
    bool fullSetPublished, bool scriptSelected, bool publicRegion,
    std::int32_t heldRegion, std::uint32_t selectedRegion,
    std::uint32_t selectedSliceSet, std::uint32_t factor) noexcept {
    return !fullSetPublished
        && ((!scriptSelected && !publicRegion)
            || !mission_seed_arrival_window_closed(
                heldRegion, selectedRegion, selectedSliceSet, factor));
}

} // namespace sunrise::server::bap::encrypted::push::activity
