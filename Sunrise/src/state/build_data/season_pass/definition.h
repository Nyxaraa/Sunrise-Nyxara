#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::state::build_data::season_pass {

/** Reward rows the installed pass declares. The shipped build carries 196. */
inline constexpr std::size_t kRewardCapacity = 256;

/** Wrapper rewards that open into a set. The shipped pass carries one per character class. */
inline constexpr std::size_t kPackageCapacity = 8;

/** Items one wrapper opens into. The shipped wrappers each carry six. */
inline constexpr std::size_t kPackageItemCapacity = 8;

/** A reward whose claim flag no mapping table addresses carries this instead of an index. */
inline constexpr std::uint16_t kUnavailableFlagIndex = 0xFFFFU;

/** One reward row of the pass, in the native order the opcode-2400 claim names by index. */
struct Reward {
    /** Authored definition hash of the granted item. */
    std::uint32_t itemHash{};
    /** Units granted. */
    std::uint32_t quantity{};
    /** Native item-definition index the row names. */
    std::uint16_t itemIndex{};
    /** Account flag bank row this reward's claim sets, or kUnavailableFlagIndex. */
    std::uint16_t claimFlagIndex{kUnavailableFlagIndex};
    /** Rank the account needs before the row may be claimed. */
    std::uint8_t requiredRank{};
};

/** One wrapper reward and the items it opens into. */
struct Package {
    /** Authored definition hash of the wrapper item. */
    std::uint32_t definitionHash{};
    /** Authored definition hashes the wrapper opens into, in declared order. */
    std::array<std::uint32_t, kPackageItemCapacity> items{};
    std::uint8_t itemCount{};
};

} // namespace sunrise::state::build_data::season_pass
