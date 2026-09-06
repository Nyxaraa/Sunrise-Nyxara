// Regression for the selected-state arrival window.
//
// An authored region is `sliceSetIndex + stateOrdinal`. Ember's apex gameplay and both ending
// bookends are sibling states of slice set 0 (regions 0, 1 and 2), while every earlier area is
// its own slice set. Only a slice-set change replaces the client's instantiated world.
//
// Treating a sibling move as a replacement is what froze the mission's ending: the roster
// withheld region 1's groups until the client reported holding region 1, and the client's
// current region leg only advances on a real slice-set switch, so the report never came.

#include <cassert>
#include <cstdint>

#include "../Sunrise/src/server/bap/encrypted/push/activity/mission_seed_world_change.h"

namespace {

namespace seed = sunrise::server::bap::encrypted::push::activity;

/** Ember's authored states, as `{sliceSetIndex, effectiveRegion}`. */
constexpr std::uint32_t kApexSliceSet = 0;
constexpr std::uint32_t kApexRegion = 0;
constexpr std::uint32_t kFirstMovieRegion = 1;
constexpr std::uint32_t kSecondMovieRegion = 2;
constexpr std::uint32_t kArrivalSliceSet = 48;
constexpr std::uint32_t kArrivalRegion = 49;
constexpr std::uint32_t kPowerhouseSliceSet = 64;
constexpr std::uint32_t kPowerhouseRegion = 64;
constexpr std::uint32_t kLinkSliceSet = 56;
constexpr std::uint32_t kLinkRegion = 56;
constexpr std::uint32_t kCinderSliceSet = 40;
constexpr std::uint32_t kCinderRegion = 40;

void sibling_states_keep_their_world() {
    // Apex gameplay to the first ending bookend: same slice set, so nothing is rebuilt.
    assert(!seed::mission_seed_region_change_replaces_world(
        kApexSliceSet, kApexRegion, kApexSliceSet, kFirstMovieRegion));
    // First bookend to the second: still slice set 0.
    assert(!seed::mission_seed_region_change_replaces_world(
        kApexSliceSet, kFirstMovieRegion, kApexSliceSet, kSecondMovieRegion));
    // And back, so a checkpoint or reselection is treated the same way.
    assert(!seed::mission_seed_region_change_replaces_world(
        kApexSliceSet, kSecondMovieRegion, kApexSliceSet, kApexRegion));
}

void slice_set_changes_replace_the_world() {
    // Every transition the mission already made is a real slice-set change and must stay one,
    // or the roster would register the new region's groups into a world being torn down.
    assert(seed::mission_seed_region_change_replaces_world(
        kArrivalSliceSet, kArrivalRegion, kPowerhouseSliceSet, kPowerhouseRegion));
    assert(seed::mission_seed_region_change_replaces_world(
        kPowerhouseSliceSet, kPowerhouseRegion, kLinkSliceSet, kLinkRegion));
    assert(seed::mission_seed_region_change_replaces_world(
        kLinkSliceSet, kLinkRegion, kCinderSliceSet, kCinderRegion));
    assert(seed::mission_seed_region_change_replaces_world(
        kCinderSliceSet, kCinderRegion, kApexSliceSet, kApexRegion));
    // Backtracking is a replacement in both directions.
    assert(seed::mission_seed_region_change_replaces_world(
        kCinderSliceSet, kCinderRegion, kLinkSliceSet, kLinkRegion));
}

void reselecting_the_same_state_is_never_a_replacement() {
    assert(!seed::mission_seed_region_change_replaces_world(
        kApexSliceSet, kApexRegion, kApexSliceSet, kApexRegion));
    assert(!seed::mission_seed_region_change_replaces_world(
        kCinderSliceSet, kCinderRegion, kCinderSliceSet, kCinderRegion));
    // A same-region selection stays a no-op even if the slice sets disagree: the region is the
    // publication key, and re-registering identical groups must not reopen an arrival window.
    assert(!seed::mission_seed_region_change_replaces_world(
        kApexSliceSet, kApexRegion, kCinderSliceSet, kApexRegion));
}

void decision_is_available_at_compile_time() {
    // The production caller is in a lock-held path, so the predicate must fold away.
    static_assert(!seed::mission_seed_region_change_replaces_world(0, 0, 0, 1));
    static_assert(seed::mission_seed_region_change_replaces_world(40, 40, 0, 0));
}

} // namespace

int main() {
    sibling_states_keep_their_world();
    slice_set_changes_replace_the_world();
    reselecting_the_same_state_is_never_a_replacement();
    decision_is_available_at_compile_time();
    return 0;
}
