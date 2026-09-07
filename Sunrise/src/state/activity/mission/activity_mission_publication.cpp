#include <algorithm>
#include <cstdint>
#include <limits>

#include "runtime.h"

namespace sunrise::state::activity::mission {

/** Projects one committed record into the value-owned form a publication would carry. */
bool project_for_publication(const Snapshot& snapshot,
                             std::uint64_t now,
                             PublishedMissionState& output) noexcept {
    output = {};
    const MissionState& state = snapshot.state;
    if (!state.programBound || state.faulted || state.variableCount > kVariableCapacity
        || state.timerCount > kTimerCapacity) {
        return false;
    }
    std::copy_n(state.variables.begin(), state.variableCount, output.variables.begin());
    std::copy_n(state.timers.begin(), state.timerCount, output.timers.begin());
    for (std::size_t index = 0; index < state.timerCount; ++index) {
        const std::uint64_t deadline = state.timers[index].deadlineTick;
        const std::uint64_t remaining = deadline > now ? deadline - now : 0;
        output.timerRemainingMs[index] = static_cast<std::uint32_t>(
            (std::min)(remaining,
                       static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
    }
    output.revision = state.revision;
    output.variableCount = state.variableCount;
    output.timerCount = state.timerCount;
    output.phase = state.phase;
    output.started = state.started;
    return true;
}

} // namespace sunrise::state::activity::mission
