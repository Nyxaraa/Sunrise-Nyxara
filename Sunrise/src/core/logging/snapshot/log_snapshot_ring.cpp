#include <Windows.h>

#include <algorithm>
#include <array>
#include <limits>

#include "core/threading/data_mutex.h"
#include "internal.h"

namespace sunrise::core::log::snapshot {
namespace {

/** One trailing byte keeps a null for debugger-friendly storage. */
constexpr std::size_t kTextTerminatorBytes = 1;

struct RingState {
    std::array<Entry, kEntryCapacity> entries{};
    std::size_t nextIndex{};
    std::size_t count{};
    std::uint64_t overwrittenCount{};
};

threading::SharedDataMutex<RingState> g_ring;

/** @param channel Value to inspect. @return True for a defined log channel. */
[[nodiscard]] bool valid_channel(Channel channel) noexcept {
    return static_cast<std::size_t>(channel) < static_cast<std::size_t>(Channel::count);
}

/** @param level Value to inspect. @return True for an emitted severity. */
[[nodiscard]] bool valid_level(Level level) noexcept {
    return level >= Level::error && level < Level::off;
}

} // namespace

/** @return Channel that accepted the event. */
Channel Entry::channel() const noexcept {
    return channel_;
}

/** @return Severity that accepted the event. */
Level Entry::level() const noexcept {
    return level_;
}

/** @return Bounded formatted event text without the sink line ending. */
std::string_view Entry::text() const noexcept {
    return {text_.data(), textLength_};
}

/** @return Retained entries ordered from oldest to newest. */
std::span<const Entry> Snapshot::entries() const noexcept {
    return {entries_.data(), count_};
}

/** @return Count of older entries replaced by the fixed ring. */
std::uint64_t Snapshot::overwritten_count() const noexcept {
    return overwrittenCount_;
}

/** @return A value-owned chronological copy of all retained events. */
Snapshot take() noexcept {
    Snapshot result;
    g_ring.lock_read([&result](const RingState& ring) {
        result.count_ = ring.count;
        result.overwrittenCount_ = ring.overwrittenCount;
        const std::size_t firstIndex =
            (ring.nextIndex + kEntryCapacity - ring.count) % kEntryCapacity;
        for (std::size_t index = 0; index < ring.count; ++index) {
            result.entries_[index] = ring.entries[(firstIndex + index) % kEntryCapacity];
        }
    });
    return result;
}

namespace internal {

/** Clears retained events before a new logger lifecycle starts. */
void reset() noexcept {
    g_ring.lock_write([](RingState& ring) {
        ring.entries = {};
        ring.nextIndex = 0;
        ring.count = 0;
        ring.overwrittenCount = 0;
    });
}

/**
 * Adds one already-enabled formatted event to the fixed logger ring.
 * @param channel Channel that accepted the event.
 * @param level Severity that accepted the event.
 * @param text Formatted event text without the sink line ending.
 */
void record(Channel channel, Level level, std::string_view text) noexcept {
    if (!valid_channel(channel) || !valid_level(level)) {
        return;
    }

    g_ring.lock_write([channel, level, text](RingState& ring) {
        Entry& entry = ring.entries[ring.nextIndex];
        entry = {};
        entry.channel_ = channel;
        entry.level_ = level;
        const std::size_t maximumText = entry.text_.size() - kTextTerminatorBytes;
        entry.textLength_ = (std::min)(text.size(), maximumText);
        if (entry.textLength_ != 0) {
            std::copy_n(text.data(), entry.textLength_, entry.text_.data());
        }
        ring.nextIndex = (ring.nextIndex + 1) % kEntryCapacity;
        if (ring.count < kEntryCapacity) {
            ++ring.count;
        } else if (ring.overwrittenCount != (std::numeric_limits<std::uint64_t>::max)()) {
            ++ring.overwrittenCount;
        }
    });
}

} // namespace internal
} // namespace sunrise::core::log::snapshot
