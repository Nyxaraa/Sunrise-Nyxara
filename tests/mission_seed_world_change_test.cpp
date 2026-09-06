// Packed regions select native entries: 4C8E60 base, 4C8E40 alternates.
#include <cassert>
#include "../Sunrise/src/server/bap/encrypted/push/activity/mission_seed_world_change.h"
namespace seed = sunrise::server::bap::encrypted::push::activity;
int main() {
    // Both movies need their own native world entry even though the bubble stays zero.
    assert(seed::mission_seed_region_change_replaces_world(0, 0, 0, 1));
    assert(seed::mission_seed_region_change_replaces_world(0, 1, 0, 2));
    assert(seed::mission_seed_region_change_replaces_world(0, 2, 0, 0));
    assert(!seed::mission_seed_region_change_replaces_world(0, 1, 0, 1));
    // Preserve ordinary mission travel and exact same-state idempotence.
    assert(seed::mission_seed_region_change_replaces_world(48, 49, 64, 64));
    assert(seed::mission_seed_region_change_replaces_world(64, 64, 56, 56));
    assert(seed::mission_seed_region_change_replaces_world(40, 40, 0, 0));
    assert(!seed::mission_seed_region_change_replaces_world(40, 40, 40, 40));
    // Held base gameplay is not a bookend arrival; pending/unknown is not held.
    assert(!seed::mission_seed_arrival_window_closed(0, 1, 0, 8));
    assert(!seed::mission_seed_arrival_window_closed(1, 2, 0, 8));
    assert(!seed::mission_seed_arrival_window_closed(-1, 1, 0, 8));
    assert(!seed::mission_seed_arrival_window_closed(1, 1, 0, 0));
    assert(seed::mission_seed_arrival_window_closed(1, 1, 0, 8));
    assert(seed::mission_seed_arrival_window_closed(2, 2, 0, 8));
    // The last scripted seed can be landing while natural traversal reached Apex.
    assert(seed::mission_seed_selection_needs_arrival(64, 64, 0, 1, 0, 8));
    assert(!seed::mission_seed_selection_needs_arrival(64, 64, 0, 1, 1, 8));
    assert(!seed::mission_seed_selection_needs_arrival(64, 64, 0, 0, 0, 8));
    // Local objects must wait for their actual world; global-only startup stays intact.
    assert(seed::mission_seed_transition_subset_only(false, true, false, 0, 1, 0, 8));
    assert(!seed::mission_seed_transition_subset_only(false, true, false, 1, 1, 0, 8));
    assert(seed::mission_seed_transition_subset_only(false, false, false, 1, 1, 0, 8));
    assert(!seed::mission_seed_transition_subset_only(false, false, true, 1, 1, 0, 8));
    assert(!seed::mission_seed_transition_subset_only(true, true, false, 0, 1, 0, 8));
}
