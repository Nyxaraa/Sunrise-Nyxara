#pragma once

#include <cstddef>

namespace sunrise::server::activity::mission {

/** Derived events one instance may dispatch per service slice. */
inline constexpr std::size_t kScriptEventBatchLimit = 64;

/**
 * Dispatches queued events while `ready` allows it, up to the batch limit. The ready test stops
 * at the first asynchronous output, because the callbacks after it must see its committed result.
 * @return Events dispatched.
 */
template <class Ready, class Dispatch>
std::size_t drain_script_event_batch(Ready ready, Dispatch dispatch) {
    std::size_t count = 0;
    while (count < kScriptEventBatchLimit && ready()) {
        dispatch();
        ++count;
    }
    return count;
}

} // namespace sunrise::server::activity::mission
