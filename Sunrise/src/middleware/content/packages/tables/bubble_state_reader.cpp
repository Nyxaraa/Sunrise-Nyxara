#include "bubble_state_reader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

#include "../../../../core/logging/log.h"
#include "component_container_reader.h"
#include "internal.h"

namespace sunrise::middleware::content::packages::tables {
namespace {

/**
 * Adds one package id to the distinct list.
 * @param output Scenario package list.
 * @param packageId Candidate id.
 */
void add_package(BubbleStates& output, std::uint16_t packageId) noexcept {
    if (packageId == kAbsentPackageId || output.packageCount >= output.packages.size()) {
        return;
    }
    for (std::size_t index = 0; index < output.packageCount; ++index) {
        if (output.packages[index] == packageId) {
            return;
        }
    }
    output.packages[output.packageCount++] = packageId;
}

/**
 * Slice-state rows reported per run, so a full package sweep cannot fill the sink.
 * 359 destinations are walked and most declare tens of bubbles, so this is a sample, not a census.
 */
constexpr std::size_t kMaxStateReports = 4096;
/** Rows already spent. */
std::atomic<std::size_t> g_stateReports{};

/**
 * Dumps one slice-set state whole, so its map-global bubble index can be located rather than
 * assumed.
 *
 * `kStateMapBubbleIndexOffset` is an inference, and the values it produces do not behave like a
 * map-global index: within one destination most bubbles resolve to zero, and the field is what a
 * container's bubble mask is keyed by, so a wrong offset collapses every bubble's spawn sets and
 * components onto whichever bubble reads zero. The bubble name hash is the same for one bubble
 * whichever destination walks it, so grouping these rows by hash decides the offset outright: the
 * right one agrees across every destination that names the bubble and differs between bubbles.
 * @param ordinal Bubble ordinal within its scenario.
 * @param nameHash The bubble's own name hash, stable across destinations.
 * @param state Raw inline bytes of slice-set state zero.
 */
void report_state(std::uint64_t ordinal,
                  std::uint32_t nameHash,
                  std::span<const std::byte> state) noexcept {
    if (!core::log::accepts(core::log::Channel::state, core::log::Level::debug)
        || g_stateReports.fetch_add(1, std::memory_order_relaxed) >= kMaxStateReports) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=build_data stage=slice_state bubble=%llu hash=0x%08X raw=",
                                static_cast<unsigned long long>(ordinal),
                                nameHash);
    for (std::size_t offset = 0; offset < state.size() && written > 0
                                 && static_cast<std::size_t>(written) + 3 < line.size();
         ++offset) {
        const int more = std::snprintf(line.data() + written,
                                       line.size() - static_cast<std::size_t>(written),
                                       "%02X",
                                       std::to_integer<unsigned char>(state[offset]));
        if (more <= 0) {
            break;
        }
        written += more;
    }
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

/** Builds one scenario's per-bubble state array. */
bool bubble_states(std::span<const std::byte> scenario, BubbleStates& output) noexcept {
    output = {};
    Array bubbles{};
    if (!scenario_bubbles(scenario, bubbles)) {
        return false;
    }
    output.truncated = bubbles.count > kBubbleStateCapacity;
    const std::uint64_t reported = output.truncated ? kBubbleStateCapacity : bubbles.count;

    for (std::uint64_t index = 0; index < reported; ++index) {
        Bubble bubble{};
        if (!bubble_at(scenario, bubbles, index, bubble)) {
            output = {};
            return false;
        }
        // A bubble with no readable state array is disabled, the same as a cleared first state.
        std::uint8_t value = kBubbleDisabledByte;
        SliceState state{};
        std::uint16_t mapIndex = kAbsentMapBubbleIndex;
        std::size_t stateOffset = 0;
        if (bubble.stateCount != 0
            && element_offset(
                bubble.stateDataOffset, bubble.stateCount, kSliceStateStride, 0, stateOffset)
            && stateOffset + kSliceStateStride <= scenario.size()) {
            report_state(index,
                         bubble.nameHash,
                         scenario.subspan(static_cast<std::size_t>(stateOffset),
                                          kSliceStateStride));
        }
        if (bubble.stateCount != 0 && slice_state_at(scenario, bubble, 0, state)) {
            value = state.enabled ? kBubbleEnabledByte : kBubbleDisabledByte;
            // An index no container mask can name is absent, because nothing could match it.
            if (state.mapBubbleIndex < kBubbleIndexCapacity) {
                mapIndex = static_cast<std::uint16_t>(state.mapBubbleIndex);
            }
        }
        // Every state names its slice-set entry, and the entry's package is one this destination
        // loads. A bubble's later states can name a package its first one does not.
        for (std::uint64_t ordinal = 0; ordinal < bubble.stateCount; ++ordinal) {
            SliceState each{};
            if (slice_state_at(scenario, bubble, ordinal, each)) {
                add_package(output, package_of(each.entryTag));
            }
        }
        const std::uint64_t states = (std::min)(bubble.stateCount, kBubbleStateCountCeiling);
        output.bytes[static_cast<std::size_t>(index)] = value;
        output.hashes[static_cast<std::size_t>(index)] = bubble.nameHash;
        output.stateCounts[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(states);
        output.mapIndices[static_cast<std::size_t>(index)] = mapIndex;
    }
    output.count = static_cast<std::size_t>(reported);
    return true;
}

} // namespace sunrise::middleware::content::packages::tables
