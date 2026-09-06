#pragma once

#include <cstdint>

namespace sunrise::server::bap::encrypted::push::activity {

/**
 * Checks whether moving to a newly selected state actually replaces the instantiated world.
 *
 * An authored region is `sliceSetIndex + stateOrdinal`, so sibling states of one slice set are
 * different regions inside the same instantiated content: Ember's apex gameplay (region 0) and
 * its two ending bookends (regions 1 and 2) all live in slice set 0. Only a slice-set change
 * tears the world down and rebuilds it.
 *
 * Treating a sibling move as a replacement deadlocks publication: the roster withholds the new
 * region's groups until the client reports holding it, while the client's current region leg
 * advances only on a real slice-set switch, so that report never arrives.
 *
 * Kept dependency-free so the decision can be exercised without the BAP session types.
 *
 * @param currentSliceSetIndex Slice set the lease's published plan belongs to.
 * @param currentEffectiveRegion That plan's authored region.
 * @param selectedSliceSetIndex Slice set the newly selected plan belongs to.
 * @param selectedEffectiveRegion The newly selected plan's authored region.
 * @return True only when the client must tear its world down and rebuild it.
 */
[[nodiscard]] constexpr bool
mission_seed_region_change_replaces_world(std::uint32_t currentSliceSetIndex,
                                          std::uint32_t currentEffectiveRegion,
                                          std::uint32_t selectedSliceSetIndex,
                                          std::uint32_t selectedEffectiveRegion) noexcept {
    return currentEffectiveRegion != selectedEffectiveRegion
           && currentSliceSetIndex != selectedSliceSetIndex;
}

/**
 * Checks whether a pending region arrival can still be reported by the client.
 *
 * The arrival window exists so a publication does not register the new region's groups into a
 * world the client is tearing down. It closes when the client reports holding the pending region.
 * A sibling state never produces that report: its content lives in the slice set the client
 * already holds, and the current region leg only advances on a slice-set switch. Leaving the
 * window open there is not cautious, it is fatal -- the roster refuses to commit a published
 * revision while it is set, so the lease never publishes and every scene lease on the new state
 * reports a pending mission seed forever.
 *
 * @param heldRegion Region the client reports holding, or negative when it holds none.
 * @param pendingEffectiveRegion Authored region the pending plan selects.
 * @param pendingSliceSetIndex Slice set that plan belongs to.
 * @param sliceSetFactor Regions per slice set; slice-set indices are multiples of it.
 * @return True once the window must close, either by arrival or because none can occur.
 */
[[nodiscard]] constexpr bool
mission_seed_arrival_window_closed(std::int32_t heldRegion,
                                   std::uint32_t pendingEffectiveRegion,
                                   std::uint32_t pendingSliceSetIndex,
                                   std::uint32_t sliceSetFactor) noexcept {
    if (heldRegion < 0 || sliceSetFactor == 0) {
        return false;
    }
    const auto held = static_cast<std::uint32_t>(heldRegion);
    return held == pendingEffectiveRegion
           || held - (held % sliceSetFactor) == pendingSliceSetIndex;
}

} // namespace sunrise::server::bap::encrypted::push::activity
