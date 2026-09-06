#include <array>
#include <cassert>
#include "../Sunrise/src/server/activity/activity_sdk_behavior_scope.h"
namespace scope = sunrise::server::activity::behavior_scope;
namespace f = sunrise::state::activity_sdk::format;
int main() {
    std::array<f::State, 4> states{};
    std::array<f::Bubble, 2> bubbles{};
    states[0].sliceSetIndex = 64; // Opening seed remains published.
    states[1].bubbleIndex = 1; // Light's End, region zero.
    states[2] = states[1]; states[2].stateOrdinal = 1; // Ending cinematic.
    states[3] = states[1]; states[3].scenarioIndex = 1;
    std::array<f::Occurrence, 5> rows{};
    rows[0].objectIndex = 7; // Shared object at opening.
    rows[1] = rows[0]; rows[1].stateIndex = 1; rows[1].bubbleIndex = 1;
    rows[2] = rows[1]; rows[2].objectIndex = 9; // Reactor-only alarm.
    rows[3] = rows[2]; rows[3].stateIndex = 2; // Cinematic-state occurrence.
    rows[4] = rows[2]; rows[4].scenarioIndex = 1; rows[4].stateIndex = 3;
    assert(scope::select(rows,states,bubbles,0,9,0,0).row == 2);
    assert(scope::select(rows,states,bubbles,0,7,0,0).row == 1);
    assert(scope::select(rows,states,bubbles,0,7,0,64).row == 0);
    assert(scope::select(rows,states,bubbles,0,9,0,1).row == 3);
    assert(scope::select(rows,states,bubbles,0,9,0,40).row == f::kAbsentIndex);
    assert(!scope::live_state(states,bubbles,0,1,64));
    assert(!scope::live_state(states,bubbles,0,3,0));
    assert(!scope::live_state(states,bubbles,0,99,0));
    assert(!scope::live_state(states,bubbles,0,1,-1));
    states[2].stateOrdinal = 0; // Conflicting authored rows must refuse.
    assert(scope::select(rows,states,bubbles,0,9,0,0).ambiguous);
}
