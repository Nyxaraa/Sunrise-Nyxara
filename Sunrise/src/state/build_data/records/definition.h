#pragma once

#include <cstddef>
#include <cstdint>

namespace sunrise::state::build_data::records {

/** The shipped build declares 2242 records and lore entries. The domain leaves room above that. */
inline constexpr std::size_t kDefinitionCapacity = 4096;

/** Objective rows across every record. The shipped build declares 2553. */
inline constexpr std::size_t kObjectiveCapacity = 4096;

/** Interval rows across every record. The shipped build declares 585. */
inline constexpr std::size_t kIntervalCapacity = 1024;

/** Reward rows across every record. The shipped build declares 145. */
inline constexpr std::size_t kRewardCapacity = 256;

/** Rewards one record may grant. The widest shipped bundle is three items. */
inline constexpr std::size_t kRewardPerRecordCapacity = 4;

/** Steps one interval ladder may declare. The widest shipped ladder has seven. */
inline constexpr std::size_t kIntervalPerRecordCapacity = 8;

/**
 * First account value index the record objective bank occupies.
 * The client places record objective values in one run starting here, in record row order. No
 * package field carries the base, so it is measured against the installed value bank.
 */
inline constexpr std::uint16_t kObjectiveValueIndexBase = 2746U;

/** A record naming no objective rows still reserves this many objective value indices. */
inline constexpr std::uint8_t kReservedObjectiveValueCount = 2;

/**
 * Account value bank row the client shows as Triumph Score.
 * It is a plain replicated value, so the host must total the score itself.
 */
inline constexpr std::uint16_t kTriumphScoreValueIndex = 2115U;

/** A record whose completion flag no mapping table addresses carries this instead of an index. */
inline constexpr std::uint16_t kUnavailableFlagIndex = 0xFFFFU;

/** A record that displays no lore carries this instead of a row. */
inline constexpr std::uint16_t kUnavailableLoreRow = 0xFFFFU;

/** A record naming no category value slot carries this instead of an index. */
inline constexpr std::uint16_t kUnavailableValueIndex = 0xFFFFU;

/** An interval granting no item carries this instead of an item index. */
inline constexpr std::uint16_t kUnavailableItemIndex = 0xFFFFU;

/** One objective row of one record. */
struct Objective {
    /** Progress the client needs before the objective reads complete. */
    std::int32_t completionValue{};
    /** Account value index this objective's own progress is replicated in. */
    std::uint16_t valueIndex{};
    /** Account value index the objective reads its progress from, or the unavailable value. */
    std::uint16_t sourceValueIndex{kUnavailableValueIndex};
    /** Raw unlock value slot behind that index, so the character bank can resolve it too. */
    std::int16_t sourceValueSlot{-1};
};

/** One interval step of a record that scores per step instead of once. */
struct Interval {
    /** Progress the step needs, taken from the objective row the step names. */
    std::int32_t completionValue{};
    /** Points redeeming this step is worth. */
    std::uint32_t score{};
    /** Item the step grants, or kUnavailableItemIndex when it grants none. */
    std::uint16_t itemIndex{kUnavailableItemIndex};
};

/** One item a record grants when it is claimed. */
struct Reward {
    /** Units granted. Always positive. */
    std::int32_t quantity{};
    /** Native item definition index. */
    std::uint16_t itemIndex{};
};

/**
 * One record reduced to what a claim needs.
 * The record row names a flag slot; extraction resolves it to a bank index once, stored here.
 * The objective, interval and reward ranges index the domain's three flat banks.
 */
struct Definition {
    /** Native record row, which is what an opcode-1801 claim names. */
    std::uint16_t definitionIndex{};
    /** Authored record definition hash, read from row offset +0x28. Row offset +0 is not it. */
    std::uint32_t definitionHash{};
    /** Account flag bank mapping row, or kUnavailableFlagIndex when the slot is unaddressable. */
    std::uint16_t completionFlagIndex{kUnavailableFlagIndex};
    /**
     * Lore row this record displays, or kUnavailableLoreRow when it displays none.
     * A book chapter names one; a book's parent triumph names none, which tells the two apart.
     */
    std::uint16_t loreRow{kUnavailableLoreRow};
    /** Points this record is worth, which the shipped table keeps at 500 or below. */
    std::uint16_t scoreValue{};
    /**
     * Account value index of the category this record names, or kUnavailableValueIndex.
     * Only a category's parent record names it, so a set row is a parent, not a chapter.
     */
    std::uint16_t categoryValueIndex{kUnavailableValueIndex};
    /** First account value index of this record's run in the objective value bank. */
    std::uint16_t objectiveValueIndex{};
    /** First row of this record's range in the flat objective bank. */
    std::uint16_t objectiveOffset{};
    /** First row of this record's range in the flat interval bank. */
    std::uint16_t intervalOffset{};
    /** First row of this record's range in the flat reward bank. */
    std::uint16_t rewardOffset{};
    /**
     * Account value index counting redeemed intervals, or kUnavailableValueIndex.
     * Read from the row, never computed: nine records place it away from their objective run.
     */
    std::uint16_t redeemedCountValueIndex{kUnavailableValueIndex};
    std::uint8_t objectiveCount{};
    std::uint8_t intervalCount{};
    std::uint8_t rewardCount{};
    /** True only when this record grants a character-equippable title. */
    bool hasTitle{};
};

} // namespace sunrise::state::build_data::records
