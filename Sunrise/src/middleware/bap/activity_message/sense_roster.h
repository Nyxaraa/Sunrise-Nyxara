#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_reader.h"

namespace sunrise::middleware::bap::activity_message::sense_update {

template <std::size_t Capacity> struct RosterKeys final {
    std::array<std::uint32_t, Capacity> keys{};
    std::array<std::uint32_t, Capacity / 32> presence{};
    std::array<std::int8_t, Capacity> states{};
    std::uint16_t keyCount{};
    std::uint16_t stateCount{};
    bool hasKeys{};
    bool hasPresence{};
    bool hasStates{};
};

struct RosterBubble final {
    RosterKeys<96> groups{};
    std::int32_t bubble{-1};
    bool hasBubble{};
};

/** Msg6's 80809445 delta. Presence flags distinguish omitted fields from empty lists. */
struct RosterDelta final {
    RosterKeys<256> topLevel{};
    std::array<RosterBubble, 64> bubbles{};
    std::uint8_t bubbleCount{};
    bool hasBubbles{};
    bool present{};
};

/** Fields positively observed on one connection, retaining native ordinal-based deltas. */
using RosterMirror = RosterDelta;

[[nodiscard]] bool read_roster_delta(encoding::bits::Reader& reader, RosterDelta& output) noexcept;
void apply_roster_delta(RosterMirror& mirror, const RosterDelta& delta) noexcept;

} // namespace sunrise::middleware::bap::activity_message::sense_update
