#include <cassert>
#include <vector>
#include "../Sunrise/src/server/activity/mission/mission_script_event_batch.h"

int main() {
    using sunrise::server::activity::mission::drain_script_event_batch;
    std::vector<unsigned> delivered;
    unsigned next = 0;
    bool outputPending = false;
    const auto ready = [&] { return next < 1000 && !outputPending; };
    const auto dispatch = [&] {
        delivered.push_back(next++);
        if (next == 71) outputPending = true;
    };
    assert(drain_script_event_batch(ready, dispatch) == 64);
    assert(drain_script_event_batch(ready, dispatch) == 7);
    assert(drain_script_event_batch(ready, dispatch) == 0);
    assert(next == 71); // The callback after a command waits for its commit.
    outputPending = false;
    unsigned ticks = 0;
    while (ready()) { drain_script_event_batch(ready, dispatch); ++ticks; }
    assert(ticks == 15 && delivered.size() == 1000);
    for (unsigned i = 0; i < delivered.size(); ++i) assert(delivered[i] == i);
}
