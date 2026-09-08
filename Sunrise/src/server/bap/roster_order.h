#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../middleware/bap/activity_message/sensor_auth_update.h"

namespace sunrise::server::bap {

namespace roster_order_wire = middleware::bap::activity_message::sensor_auth_update;

struct RosterOrder {
    struct Block {
        std::uint32_t bubble{};
        std::array<std::uint32_t, roster_order_wire::kBubbleKeyCapacity> keys{};
        std::array<std::uint32_t, (roster_order_wire::kBubbleKeyCapacity + 31) / 32> presence{};
        std::uint16_t count{};
    };
    std::array<Block, roster_order_wire::kBubbleSubBlockCapacity> blocks{};
    std::uint64_t bindingGeneration{};
    std::uint16_t count{};
};

// The native reconciler compares both bubble blocks and keys by ordinal.
[[nodiscard]] inline bool preserve_roster_order(
    const RosterOrder& published, std::uint64_t generation,
    std::span<const roster_order_wire::BubbleSubBlock> candidate,
    RosterOrder& output) noexcept {
    output = published.bindingGeneration == generation ? published : RosterOrder{};
    output.bindingGeneration = generation;
    if (output.count > output.blocks.size() || candidate.size() > output.blocks.size()) return false;
    for (std::size_t i = 0; i < output.count; ++i) output.blocks[i].presence = {};
    for (std::size_t i = 0; i < candidate.size(); ++i) {
        const auto& input = candidate[i];
        for (std::size_t j = 0; j < i; ++j) {
            if (candidate[j].bubble == input.bubble) return false;
        }
        auto end = output.blocks.begin() + output.count;
        auto block = std::find_if(output.blocks.begin(), end,
                                 [&](const auto& b) { return b.bubble == input.bubble; });
        if (block == end) {
            if (output.count == output.blocks.size()) return false;
            block = output.blocks.begin() + output.count++;
            *block = {};
            block->bubble = input.bubble;
        }
        if (block->count > block->keys.size()) return false;
        for (std::size_t k = 0; k < input.keys.size(); ++k) {
            const auto key = input.keys[k];
            const auto priorEnd = input.keys.begin() + k;
            if (std::find(input.keys.begin(), priorEnd, key) != priorEnd) return false;
            auto last = block->keys.begin() + block->count;
            auto found = std::find(block->keys.begin(), last, key);
            if (found == last) {
                if (block->count == block->keys.size()) return false;
                *found = key;
                ++block->count;
            }
            const auto ordinal = static_cast<std::size_t>(found - block->keys.begin());
            block->presence[ordinal / 32] |= std::uint32_t{1} << (ordinal % 32);
        }
    }
    return true;
}

} // namespace sunrise::server::bap
