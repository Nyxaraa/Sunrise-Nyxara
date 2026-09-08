#include "sense_roster.h"

#include <bit>

namespace sunrise::middleware::bap::activity_message::sense_update {
namespace {
using Reader = encoding::bits::Reader;

bool presence(Reader& reader, bool& value) noexcept {
    std::uint64_t wire{};
    if (!reader.read(1, wire)) return false;
    value = wire != 0;
    return true;
}

template <std::size_t Capacity>
bool read_keys(Reader& reader, RosterKeys<Capacity>& output, std::uint8_t width) noexcept {
    std::uint64_t wire{};
    if (!presence(reader, output.hasKeys)) return false;
    if (output.hasKeys) {
        if (!reader.read(width, wire) || wire > Capacity) return false;
        output.keyCount = static_cast<std::uint16_t>(wire);
        for (std::size_t i = 0; i < output.keyCount; ++i) {
            if (!reader.read(32, wire)) return false;
            output.keys[i] = static_cast<std::uint32_t>(wire);
        }
    }
    if (!presence(reader, output.hasPresence)) return false;
    if (output.hasPresence) {
        for (auto& word : output.presence) {
            if (!reader.read(32, wire)) return false;
            word = static_cast<std::uint32_t>(wire);
        }
    }
    if (!presence(reader, output.hasStates)) return false;
    if (output.hasStates) {
        if (!reader.read(width, wire) || wire > Capacity) return false;
        output.stateCount = static_cast<std::uint16_t>(wire);
        for (std::size_t i = 0; i < output.stateCount; ++i) {
            if (!reader.read(8, wire)) return false;
            output.states[i] = static_cast<std::int8_t>(static_cast<int>(wire) - 128);
        }
    }
    return true;
}

template <std::size_t Capacity>
void merge_keys(RosterKeys<Capacity>& mirror, const RosterKeys<Capacity>& delta) noexcept {
    if (delta.hasKeys) {
        mirror.keys = delta.keys;
        mirror.keyCount = delta.keyCount;
        mirror.hasKeys = true;
    }
    if (delta.hasPresence) {
        mirror.presence = delta.presence;
        mirror.hasPresence = true;
    }
    if (delta.hasStates) {
        mirror.states = delta.states;
        mirror.stateCount = delta.stateCount;
        mirror.hasStates = true;
    }
}

} // namespace

bool read_roster_delta(Reader& reader, RosterDelta& output) noexcept {
    output = {};
    if (!presence(reader, output.present)) return false;
    if (!output.present) return true;
    bool topLevel{};
    if (!presence(reader, topLevel) || (topLevel && !read_keys(reader, output.topLevel, 9))
        || !presence(reader, output.hasBubbles))
        return false;
    if (!output.hasBubbles) return true;
    std::uint64_t wire{};
    if (!reader.read(7, wire) || wire > output.bubbles.size()) return false;
    output.bubbleCount = static_cast<std::uint8_t>(wire);
    for (std::size_t i = 0; i < output.bubbleCount; ++i) {
        auto& row = output.bubbles[i];
        bool groups{};
        if (!presence(reader, row.hasBubble)) return false;
        if (row.hasBubble) {
            if (!reader.read(32, wire)) return false;
            row.bubble =
                std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(wire) - 0x80000000U);
        }
        if (!presence(reader, groups) || (groups && !read_keys(reader, row.groups, 7)))
            return false;
    }
    return true;
}

void apply_roster_delta(RosterMirror& mirror, const RosterDelta& delta) noexcept {
    if (!delta.present) return;
    merge_keys(mirror.topLevel, delta.topLevel);
    if (delta.hasBubbles) {
        mirror.hasBubbles = true;
        mirror.bubbleCount = delta.bubbleCount;
        for (std::size_t i = 0; i < delta.bubbleCount; ++i) {
            auto& target = mirror.bubbles[i];
            const auto& source = delta.bubbles[i];
            if (source.hasBubble) {
                target.bubble = source.bubble;
                target.hasBubble = true;
            }
            merge_keys(target.groups, source.groups);
        }
    }
    mirror.present = true;
}

} // namespace sunrise::middleware::bap::activity_message::sense_update
