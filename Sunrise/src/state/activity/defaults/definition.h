#pragma once

#include <cstddef>
#include <cstdint>

#include "../destination/definition.h"

namespace sunrise::state::activity::defaults {

/** Global activity state reserves 64 one-byte bubble-state entries. */
inline constexpr std::size_t kBubbleCapacity = 64;
/** Each bubble owns 8 consecutive slice-state indices. */
inline constexpr std::size_t kSliceStatesPerBubble = 8;
/** One bubble is needed for a usable locally-authored destination policy. */
inline constexpr std::uint8_t kMinimumBubbleCount = 1;
/** 64 bubbles with 8 states each give a largest valid index of 511. */
inline constexpr std::uint16_t kMaximumInitialSliceSet = 511;

/** Small numeric launch policy paired with the locally-authored default destination. */
struct FallbackPolicy final {
    /** Number of meaningful entries in the fixed bubble-state array. */
    std::uint8_t bubbleCount{};
    /** One bit per bubble; set bits mark entries that publish slice-state zero. */
    std::uint64_t statefulBubbleMask{};
    /** First slice-state index, picked when no earlier source finds one. */
    std::uint16_t initialSliceSet{};
    /** Spawn-set name hash used by the initial slice-state selection. */
    std::uint32_t spawnSetHash{};
};

/** One whole absent-selection fallback without a package-content map. */
struct DefaultDestination final {
    destination::DestinationSelection selection{};
    FallbackPolicy fallback{};
};

/**
 * Destinations that may carry an authored arrival override.
 * A few maps bind their arrival when the map loads instead of declaring it in the packages, so no
 * walk can derive those. The reference set is 20 rows. This leaves room above it.
 */
inline constexpr std::size_t kArrivalOverrideCapacity = 64;

/**
 * One authored arrival for a named destination, applied over every derived source.
 * Every field is optional: a row may override the bubble, exact slice set, or spawn set. A bubble
 * owns one slice set per authored state, so naming it reaches only the first; `slice_set` picks.
 */
struct ArrivalOverride final {
    std::array<char, destination::kPackageNameCapacity> name{};
    std::uint8_t nameLength{};
    std::uint8_t bubble{};
    bool hasBubble{};
    /** Slice set to arrive in. Must be one of the arrival bubble's own run. */
    std::uint16_t sliceSet{};
    bool hasSliceSet{};
    std::uint32_t spawnSetHash{};
    bool hasSpawnSetHash{};
    /**
     * A launch into this destination makes it the character's current activity (family-4
     * `+45896`) before the client commits the launch, so the fly-in legs play their black variant.
     */
    bool currentActivityFromLaunch{};
};

/** Immutable activity defaults supplied while the root State is initialized. */
struct ActivityDefaults final {
    DefaultDestination defaultDestination{};
    std::array<ArrivalOverride, kArrivalOverrideCapacity> arrivalOverrides{};
    std::uint8_t arrivalOverrideCount{};
    /**
     * Sends the membership identity's `field3` as message 5's player key, not the character SOID.
     * That field is the member record's `+16`, which is the value this key must equal.
     */
    bool rosterKeyFromIdentity{};
    /**
     * Fills message 5's participation body on every type-13 slot of the key group.
     * The old encoder fills only the group's first, and the gate reads whichever object the player
     * datum names, which need not be that one.
     */
    bool rosterKeyOnAllSlots{};
    /**
     * Author the type-35 mission-director and type-18 script-runtime auth bodies.
     * On by default: they are what an encounter bubble's script objects come from, and shipping
     * them bodyless is why Last Wish's encounter bubbles create no objects. Turn off to restore
     * the previous behaviour without a rebuild if a body ever desynchronises the phase-2 stream,
     * whose symptom is the player failing to spawn at all rather than only the encounter failing.
     */
    bool authorDirectorBodies{true};
    /** Fill the type-37 auth body. See `authorDirectorBodies` for the width-gate rationale. */
    bool authorWideRecordBodies{true};
};

} // namespace sunrise::state::activity::defaults
