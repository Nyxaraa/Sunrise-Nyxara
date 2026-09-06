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

} // namespace sunrise::server::bap::encrypted::push::activity
