#pragma once

#include <algorithm>
#include <span>

#include "activity_roster_mirror.h"

namespace sunrise::server::bap {

struct MissionRetiredGroup {
    std::uint32_t key{};
    std::uint16_t ordinal{};
    std::uint8_t bubble{64};
    std::uint8_t state{};
};

// Native removal resolves the incoming key at the old ordinal, so retain that exact prefix.
[[nodiscard]] inline bool order_retiring_keys(std::span<const std::uint32_t> published,
                                             std::span<const std::uint32_t> candidate,
                                             std::span<std::uint32_t> output,
                                             std::size_t& count) noexcept {
    count = 0;
    if (published.size() > output.size() || candidate.size() > output.size()) return false;
    for (const auto key : published) {
        if (std::count(published.begin(), published.end(), key) != 1) return false;
        output[count++] = key;
    }
    for (const auto key : candidate) {
        if (std::count(candidate.begin(), candidate.end(), key) != 1) return false;
        if (std::find(published.begin(), published.end(), key) == published.end()) {
            if (count == output.size()) return false;
            output[count++] = key;
        }
    }
    return true;
}

template <std::size_t Capacity>
[[nodiscard]] bool retirement_state_at_ordinal(
    const middleware::bap::activity_message::sense_update::RosterKeys<Capacity>& keys,
    std::uint32_t key, std::size_t ordinal, std::uint8_t& state) noexcept {
    if (!keys.hasKeys || !keys.hasStates || ordinal >= Capacity
        || ordinal >= keys.keyCount || ordinal >= keys.stateCount || keys.keys[ordinal] != key) {
        return false;
    }
    state = static_cast<std::uint8_t>(keys.states[ordinal]);
    return true;
}

template <std::size_t Capacity>
[[nodiscard]] bool retired_at_ordinal(
    const middleware::bap::activity_message::sense_update::RosterKeys<Capacity>& keys,
    const MissionRetiredGroup& target) noexcept {
    const auto ordinal = target.ordinal;
    return keys.hasKeys && keys.hasPresence && keys.hasStates
           && ordinal < keys.keyCount && ordinal < keys.stateCount && ordinal < Capacity
           && keys.keys[ordinal] == target.key
           && static_cast<std::uint8_t>(keys.states[ordinal]) == target.state
           && (keys.presence[ordinal / 32] & (std::uint32_t{1} << (ordinal % 32))) == 0;
}

[[nodiscard]] inline bool roster_retirement_received(
    const ActivityRosterMirror& mirror,
    std::span<const MissionRetiredGroup> targets,
    std::uint64_t bindingGeneration,
    std::uint64_t receiptFloor,
    std::int32_t region) noexcept {
    if (region < 0 || mirror.bindingGeneration != bindingGeneration
        || mirror.receivedRevision <= receiptFloor || mirror.instantiatedRegion != region
        || mirror.currentRegion != region) {
        return false;
    }
    for (const auto& target : targets) {
        if (target.bubble == 64) {
            if (!retired_at_ordinal(mirror.roster.topLevel, target)) return false;
            continue;
        }
        if (!mirror.roster.hasBubbles || mirror.roster.bubbleCount > mirror.roster.bubbles.size()) return false;
        const middleware::bap::activity_message::sense_update::RosterBubble* block = nullptr;
        for (std::size_t index = 0; index < mirror.roster.bubbleCount; ++index) {
            const auto& candidate = mirror.roster.bubbles[index];
            if (candidate.hasBubble && candidate.bubble == target.bubble) {
                if (block != nullptr) return false;
                block = &candidate;
            }
        }
        if (block == nullptr || !retired_at_ordinal(block->groups, target)) return false;
    }
    return true;
}

} // namespace sunrise::server::bap
