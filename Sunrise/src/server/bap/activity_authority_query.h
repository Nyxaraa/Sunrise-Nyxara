#pragma once

#include <array>
#include <cstdint>

#include "../../middleware/bap/activity_message/entity_slots.h"

namespace sunrise::server::bap {

/** Stable result of one complete msg-30 authority-mask query. */
struct ActivityAuthorityQuerySnapshot final {
    using SlotMask = middleware::bap::activity_message::entity_slots::EntitySlotMask;

    std::array<SlotMask, 65> bubbleMasks{};
    SlotMask authorityMask{};
    std::array<bool, 65> bubblePresent{};
    std::uint64_t bindingGeneration{};
    std::int32_t correlation{-1};
    std::uint8_t bubbleResponseCount{};
    std::uint8_t lastBubble{};
    bool complete{};
};

/** Stable transport-side outcome for one connection-owned authority query. */
enum class ActivityAuthorityQueryStatus : std::uint8_t {
    queued,
    complete,
    noActivityLink,
    staleActivityClient,
    busy,
    pending,
    timedOut,
    noResult,
};

} // namespace sunrise::server::bap
