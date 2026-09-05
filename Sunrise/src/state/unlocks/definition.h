#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../build_data/progressions/definition.h"

namespace sunrise::state::unlocks {

/** The account acquired-flag bank holds one byte per flag. */
inline constexpr std::size_t kAccountFlagCapacity = 12'300;
/** The profile unlock bank holds one byte per flag. */
inline constexpr std::size_t kProfileFlagCapacity = 512;
/** Each per-character unlock block holds one byte per flag. */
inline constexpr std::size_t kCharacterFlagCapacity = 256;
/** The account objective bank holds one signed value per objective. */
inline constexpr std::size_t kObjectiveValueCapacity = 6'200;
/** The selected-character object carries its own acquired-flag bank. */
inline constexpr std::size_t kCharacterObjectFlagCapacity = 4'096;
/** The selected-character object carries its own objective bank. */
inline constexpr std::size_t kCharacterObjectValueCapacity = 768;

/** A native progression row carries 3 signed value lanes. */
inline constexpr std::size_t kProgressionLaneCount = 3;

/** The value lanes of one progression. Lane 0 is the progress the level walk reads. */
using ProgressionLanes = std::array<std::int32_t, kProgressionLaneCount>;

/** One scope's authored progression lanes, addressed by definition index. */
using ProgressionBank = std::array<ProgressionLanes, build_data::progressions::kDefinitionCapacity>;

/** A set acquired flag is stored as its biased 2-bit true value. */
inline constexpr std::uint8_t kFlagSet = 2;

/**
 * A clear acquired flag is stored as zero.
 *
 * The flag is a 2-bit field and encodes redeemed state only: all four values were measured (0 and
 * 1 show nothing, 2 -- kFlagSet -- shows claimed, 3 shows nothing) and none of them means
 * claimable. Claimable is carried by the objective bank instead: a record reads claimable when its
 * objective value equals its completionValue while this flag stays clear. The record-claim
 * account projection writes that objective state.
 */
inline constexpr std::uint8_t kFlagClear = 0;

/**
 * Authored unlock policy published once for the process.
 * Every bank is expanded at parse time, so readers copy bytes with no run decoding.
 */
struct Table {
    std::array<std::uint8_t, kAccountFlagCapacity> accountFlags{};
    std::array<std::uint8_t, kProfileFlagCapacity> profileFlags{};
    std::array<std::uint8_t, kCharacterFlagCapacity> characterFlags{};
    std::array<std::int32_t, kObjectiveValueCapacity> objectiveValues{};
    std::array<std::uint8_t, kCharacterObjectFlagCapacity> characterObjectFlags{};
    std::array<std::int32_t, kCharacterObjectValueCapacity> characterObjectValues{};
    /** Lanes published into the account object's progression bank. */
    ProgressionBank accountProgressions{};
    /** Lanes published into the selected-character object's progression bank. */
    ProgressionBank characterProgressions{};
};

} // namespace sunrise::state::unlocks
