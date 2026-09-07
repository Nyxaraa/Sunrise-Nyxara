#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "scenario_reader.h"

namespace sunrise::middleware::content::packages::tables {

/**
 * A destination reaches at most this many slice sets.
 * Slice-set indices are spaced by the slice-set factor across a 512-wide region space, so this
 * is the hard bound. The widest installed destination reaches 47.
 */
inline constexpr std::size_t kSliceSetCapacity = 64;
/**
 * Roster keys tracked for one destination.
 * No installed destination reaches more than 2 objects carrying a wire slot type. This leaves
 * room to spare without a heap allocation.
 */
inline constexpr std::size_t kRosterKeyCapacity = 16;

static_assert(kSliceSetCapacity * kSliceSetIndexFactor == 512);
// One bit per slice set, so the mask must cover the whole capacity.
static_assert(kSliceSetCapacity == 64);

/**
 * Which authored states and bubbles each candidate key appears in.
 * A key is safe to send for a bubble only when every authored state of that bubble carries it.
 * Two states may share one slice-set index, so a region-bit union alone is not sufficient.
 */
struct RosterIntersection {
    std::array<std::uint32_t, kRosterKeyCapacity> keys{};
    /** Bubbles where each key appeared in at least one state. */
    std::array<std::uint64_t, kRosterKeyCapacity> masks{};
    /** Number of authored states carrying each key, over the whole destination. */
    std::array<std::size_t, kRosterKeyCapacity> keyStateCounts{};
    /** Per-key authored-state counts in each bubble. */
    std::array<std::array<std::size_t, kSliceSetCapacity>, kRosterKeyCapacity>
        keyBubbleStateCounts{};
    /** Last authored-state serial that recorded each key, to collapse registry duplicates. */
    std::array<std::size_t, kRosterKeyCapacity> lastKeyState{};
    /** Number of authored states observed in each bubble. */
    std::array<std::size_t, kSliceSetCapacity> bubbleStateCounts{};
    std::size_t keyCount{};
    std::size_t stateCount{};
    std::uint32_t currentBubble{};
    /** Every slice set observed, whichever key carried it. */
    std::uint64_t observedSets{};
    /** Set after a valid authored-state observation and cleared only with the accumulator. */
    bool stateOpen{};
    /** Set when a key or a slice set did not fit, which makes the result unusable. */
    bool overflowed{};
    /** Set when a state's slice set could not be read, which makes every key unsafe. */
    bool unresolvedSet{};
};

/**
 * Records a slice set whose entry could not be read.
 * The destination still moves into that slice set and no key can be proved present in it, so no
 * key of this destination is safe to send afterwards.
 * @param state Accumulator for one destination.
 */
void observe_unresolved_slice_set(RosterIntersection& state) noexcept;

/**
 * Object keys admitted whatever slot types they declare.
 * Widening `kRosterSlotTypes` instead overflows `kRosterKeyCapacity` on most destinations, which
 * costs those destinations every group they publish.
 */
inline constexpr std::array<std::uint32_t, 1> kForcedRosterKeys = {0x432A36E6U};

/**
 * The slot types that make an object worth publishing as a roster group.
 * The key limit holds only for this filtered set; every placed object overflows most destinations.
 */
inline constexpr std::array<std::uint16_t, 9> kRosterSlotTypes = {
    8, 13, 16, 17, 21, 35, 37, 41, 67};

/**
 * Tests whether one placed object is a roster candidate.
 * @param object Whole placed-object bytes.
 * @return True when it declares a slot of one of the wire types.
 */
[[nodiscard]] bool carries_roster_slot(std::span<const std::byte> object) noexcept;

/**
 * Begins one authored state in a slice set, whether or not it holds a roster object.
 * Repeated slice-set indices are distinct states and all count toward bubble safety.
 * @param state Accumulator for one destination.
 * @param sliceSetIndex Slice-set index as the entry reports it, already scaled by the factor.
 * @return True when the index is inside the region space.
 */
[[nodiscard]] bool observe_slice_set(RosterIntersection& state,
                                     std::uint32_t sliceSetIndex) noexcept;

/**
 * Records that one key appears in the current authored state.
 * @param state Accumulator for one destination.
 * @param sliceSetIndex Slice-set index as the entry reports it, already scaled by the factor.
 * @param objectKey Registry key of the placed object.
 * @return True when the observation matches the current state and was recorded.
 */
[[nodiscard]] bool observe_roster_key(RosterIntersection& state,
                                      std::uint32_t sliceSetIndex,
                                      std::uint32_t objectKey) noexcept;

/**
 * Reports the keys present in every authored state of the destination.
 * @param state Accumulator for one destination.
 * @param output Receives the safe keys.
 * @param count Receives how many were written.
 * @return True when nothing overflowed and every safe key fits the output.
 */
[[nodiscard]] bool safe_roster_keys(const RosterIntersection& state,
                                    std::span<std::uint32_t> output,
                                    std::size_t& count) noexcept;

/**
 * Reports keys present in every authored state of some bubbles and not the whole destination.
 * A key missing from even one state of a bubble is excluded from that bubble's sub-block.
 * @param state Accumulator for one destination.
 * @param keys Receives the partially present keys.
 * @param masks Receives each key's bubbles, one bit per bubble index, in the same order.
 * @param count Receives how many were written.
 * @return True when nothing overflowed and every partial key fits the output.
 */
[[nodiscard]] bool partial_roster_keys(const RosterIntersection& state,
                                       std::span<std::uint32_t> keys,
                                       std::span<std::uint64_t> masks,
                                       std::size_t& count) noexcept;

} // namespace sunrise::middleware::content::packages::tables
