#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../bounties/definition.h"

namespace sunrise::state::build_data::vendors {

/**
 * Vendors that sell an endless bounty, and the pool each one rolls from.
 * The trigger row sells a dummy item, so no package field ties it to the pool it stands for. The
 * three identifiers per row are authored; the pool itself is extracted.
 */
struct RepeatableTrigger {
    /** Vendor definition hash that owns the trigger row. */
    std::uint32_t vendorHash{};
    /** Vendor category the trigger row sits in. */
    std::int32_t categoryIndex{};
    /** Item-type pair every bounty in this vendor's pool shares. */
    bounties::ItemType itemType{};
};

/**
 * The seven trigger rows whose pool the item-type separates.
 * Eva's Solstice, Dawning and Revelry rows share one item-type with 525 items, so those three
 * pools stay authored elsewhere.
 */
inline constexpr std::array<RepeatableTrigger, 7> kRepeatableTriggers{{
    // Banshee-44
    {0x280FB4FDU, 5, {29U, 0xB10F785DU}},
    // Commander Zavala
    {0x04243655U, 0, {5U, 0x68768F96U}},
    // Lord Shaxx
    {0xD6C4CCA1U, 4, {20U, 0xB10F785DU}},
    // The Drifter
    {0x0ED2CB2FU, 0, {22U, 0xB10F785DU}},
    // Saint-14
    {0x2D9E6DC1U, 9, {223U, 0x4C6468DFU}},
    // Prismatic Recaster
    {0xEE0F473EU, 1, {3074U, 0x613CD27BU}},
    // Eva Levante, Festival of the Lost
    {0x36D32C3CU, 27, {5U, 0xEBB2E829U}},
}};

/**
 * Finds the pool key one vendor category rolls from.
 * @param vendorHash Vendor definition hash.
 * @param categoryIndex Vendor category of the sale row that was bought.
 * @return The trigger, or null when that category sells no endless bounty.
 */
[[nodiscard]] constexpr const RepeatableTrigger*
find_repeatable_trigger(std::uint32_t vendorHash, std::int32_t categoryIndex) noexcept {
    for (const RepeatableTrigger& trigger : kRepeatableTriggers) {
        if (trigger.vendorHash == vendorHash && trigger.categoryIndex == categoryIndex) {
            return &trigger;
        }
    }
    return nullptr;
}

} // namespace sunrise::state::build_data::vendors
