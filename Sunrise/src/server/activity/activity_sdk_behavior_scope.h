#pragma once
#include <span>
#include "../../state/activity_sdk/format.h"

namespace sunrise::server::activity::behavior_scope {
namespace f = state::activity_sdk::format;

// A mission seed is published at the opening area. Later bubbles keep that lease,
// but their local sequences/scenes belong to the state selected by the live region.
[[nodiscard]] inline bool live_state(std::span<const f::State> states,
    std::span<const f::Bubble> bubbles, std::uint32_t scenario, std::uint32_t row,
    std::int32_t region) noexcept {
    if (region < 0 || row >= states.size()) return false;
    const auto& s = states[row];
    if (s.scenarioIndex != scenario || s.bubbleIndex >= bubbles.size()
        || bubbles[s.bubbleIndex].scenarioIndex != scenario || s.stateOrdinal >= 8) return false;
    return std::uint64_t(s.sliceSetIndex) + s.stateOrdinal == std::uint32_t(region);
}

struct Selection { std::uint32_t row{f::kAbsentIndex}; bool ambiguous{}; };
[[nodiscard]] inline Selection select(std::span<const f::Occurrence> occurrences,
    std::span<const f::State> states, std::span<const f::Bubble> bubbles,
    std::uint32_t scenario, std::uint32_t object, std::uint32_t seedState,
    std::int32_t region) noexcept {
    Selection result{};
    unsigned best{};
    for (std::uint32_t i = 0; i < occurrences.size(); ++i) {
        const auto& o = occurrences[i];
        if (o.scenarioIndex != scenario || o.objectIndex != object
            || o.stateIndex >= states.size() || states[o.stateIndex].scenarioIndex != scenario
            || o.bubbleIndex != states[o.stateIndex].bubbleIndex) continue;
        const unsigned rank = live_state(states, bubbles, scenario, o.stateIndex, region) ? 2
            : o.stateIndex == seedState ? 1 : 0;
        if (!rank || rank < best) continue;
        if (rank > best) { result = {i, false}; best = rank; }
        else if (occurrences[result.row].stateIndex != o.stateIndex) result.ambiguous = true;
    }
    return result;
}
}
