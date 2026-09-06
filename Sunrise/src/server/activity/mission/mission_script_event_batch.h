#pragma once
#include <cstddef>

namespace sunrise::server::activity::mission {
// Stop at the first asynchronous output. Callbacks after it must observe its
// committed result, but callbacks producing no output need not spend a tick each.
template<class Ready, class Dispatch>
std::size_t drain_script_event_batch(Ready ready, Dispatch dispatch) {
    constexpr std::size_t limit = 64;
    std::size_t count = 0;
    while (count < limit && ready()) {
        dispatch();
        ++count;
    }
    return count;
}
}
