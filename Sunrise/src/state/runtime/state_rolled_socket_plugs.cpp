/** Rolled socket plugs: answers a socket's action plug with a result plug from its roll set. */

#include "state_rolled_socket_plugs.h"

#include <algorithm>
#include <array>

#include "../build_data/runtime.h"

namespace sunrise::state::runtime::detail {
namespace {

namespace item_details = build_data::items::details;
namespace items = build_data::items;

/** Result plugs of one category, in installed-table order. */
struct ResultSet {
    /** Most result plugs any one category carries in the installed table. */
    static constexpr std::size_t kCapacity = 64;
    std::array<items::Definition, kCapacity> results{};
    std::size_t count{};
    /** True when at least one result stands for another plug. */
    bool linking{};
};

/** One splitmix64 step, enough spread for a cosmetic roll. */
[[nodiscard]] std::uint64_t mix(std::uint64_t value) noexcept {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

/** @return The installed detail of one definition, or false when it is not configured. */
[[nodiscard]] bool detail_of(const items::Definition& definition,
                             item_details::Definition& detail) noexcept {
    return build_data::find_configured_item_detail(definition.definitionIndex, detail)
           && detail.definitionHash == definition.definitionHash;
}

/**
 * Collects every rolled result plug sharing one category. Results are never in a socket pool;
 * one that is would be an ordinary plug and is left out.
 */
[[nodiscard]] bool collect_results(std::uint32_t plugCategoryHash, ResultSet& set) noexcept {
    set = {};
    if (plugCategoryHash == 0) {
        return false;
    }
    const std::size_t count = build_data::item_definition_count();
    for (std::size_t index = 0; index < count && index <= 0xFFFFU; ++index) {
        items::Definition definition{};
        if (!build_data::find_item_definition_index(static_cast<std::uint16_t>(index), definition)
            || definition.plugCategoryHash != plugCategoryHash || !items::rolled_result(definition)
            || build_data::is_socket_plug_pooled(definition.definitionIndex)) {
            continue;
        }
        if (set.count >= set.results.size()) {
            return false;
        }
        if (definition.linkedPlugIndex != items::kUnavailableLinkedPlugIndex) {
            set.linking = true;
        }
        set.results[set.count++] = definition;
    }
    return set.count != 0;
}

/** @return True when every stat the plug contributes is one the target declares. */
[[nodiscard]] bool plug_fits_target(const item_details::Definition& plug,
                                    const item_details::Definition& target) noexcept {
    if (plug.statCount == 0 || plug.statCount > plug.stats.size()
        || target.statCount > target.stats.size()) {
        return false;
    }
    for (std::size_t index = 0; index < plug.statCount; ++index) {
        const std::uint8_t row = plug.stats[index].row;
        const bool declared =
            std::any_of(target.stats.begin(),
                        target.stats.begin() + static_cast<std::ptrdiff_t>(target.statCount),
                        [row](const item_details::Stat& stat) { return stat.row == row; });
        if (!declared) {
            return false;
        }
    }
    return true;
}

/** @return True when one of the target's socket pools offers this plug, naming the lane. */
[[nodiscard]] bool target_offers(const item_details::Definition& target,
                                 std::uint16_t plugIndex,
                                 std::uint8_t& lane) noexcept {
    for (std::uint8_t candidate = 0; candidate < target.ordinarySocketCount; ++candidate) {
        if (build_data::is_socket_plug_allowed(target.definitionIndex, candidate, plugIndex)) {
            lane = candidate;
            return true;
        }
    }
    return false;
}

/**
 * Linked results are authored in groups of three consecutive rows, one group per class and
 * slot, each row standing for a different stat focus. Delegate-less rows sit between groups
 * and are skipped, so the group of a linked result is its ordinal among linked results / 3.
 */
constexpr std::size_t kLinkedGroupSize = 3;

/** @return The ordinal of one result among the linked results of its set, or the count. */
[[nodiscard]] std::size_t linked_ordinal(const ResultSet& set, std::size_t resultIndex) noexcept {
    std::size_t ordinal = 0;
    for (std::size_t index = 0; index < set.count; ++index) {
        if (set.results[index].linkedPlugIndex == items::kUnavailableLinkedPlugIndex) {
            continue;
        }
        if (index == resultIndex) {
            return ordinal;
        }
        ++ordinal;
    }
    return set.count;
}

/** @return The lane on the target offering any linked perk of one result's group, if any. */
[[nodiscard]] bool group_lane(const ResultSet& set,
                              std::size_t resultIndex,
                              const item_details::Definition& target,
                              std::uint8_t& lane) noexcept {
    const std::size_t ordinal = linked_ordinal(set, resultIndex);
    if (ordinal >= set.count) {
        return false;
    }
    const std::size_t group = ordinal / kLinkedGroupSize;
    for (std::size_t index = 0; index < set.count; ++index) {
        const items::Definition& other = set.results[index];
        if (other.linkedPlugIndex == items::kUnavailableLinkedPlugIndex
            || linked_ordinal(set, index) / kLinkedGroupSize != group) {
            continue;
        }
        if (target_offers(target, other.linkedPlugIndex, lane)) {
            return true;
        }
    }
    return false;
}

/** Fills the outcome's linked perk from one result, when the result stands for one. */
[[nodiscard]] bool
resolve_link(const items::Definition& result, std::uint8_t lane, RolledPlug& rolled) noexcept {
    rolled.linkedLane = lane;
    rolled.linkedPerkHash = 0;
    if (result.linkedPlugIndex == items::kUnavailableLinkedPlugIndex) {
        return true;
    }
    items::Definition perk{};
    if (!build_data::find_item_definition_index(result.linkedPlugIndex, perk)
        || perk.definitionIndex != result.linkedPlugIndex) {
        return false;
    }
    rolled.linkedPerkHash = perk.definitionHash;
    return true;
}

} // namespace

/** Classifies one requested plug for one target lane. */
RolledPlugAction classify_rolled_plug(const items::Definition& requested,
                                      const items::Definition& target,
                                      std::uint8_t lane) noexcept {
    item_details::Definition detail{};
    if (requested.plugCategoryHash == 0 || items::rolled_result(requested)
        || !detail_of(requested, detail) || detail.statCount != 0
        || !build_data::is_socket_plug_allowed(
            target.definitionIndex, lane, requested.definitionIndex)) {
        return RolledPlugAction::none;
    }
    ResultSet set{};
    return collect_results(requested.plugCategoryHash, set) ? RolledPlugAction::roll
                                                            : RolledPlugAction::none;
}

bool is_rolled_result(std::uint32_t plugHash) noexcept {
    items::Definition definition{};
    return plugHash != 0 && build_data::find_item_definition_hash(plugHash, definition)
           && definition.definitionHash == plugHash && items::rolled_result(definition);
}

/** Rolls one result plug for a target item. */
bool roll_socket_plug(const items::Definition& requested,
                      const item_details::Definition& target,
                      std::uint8_t characterClass,
                      std::uint32_t currentPlugHash,
                      std::uint64_t seed,
                      RolledPlug& rolled) noexcept {
    rolled = {};
    ResultSet set{};
    if (!collect_results(requested.plugCategoryHash, set)) {
        return false;
    }

    std::array<std::size_t, ResultSet::kCapacity> pick{};
    std::array<std::uint8_t, ResultSet::kCapacity> pickLanes{};
    std::size_t pickCount = 0;
    // A delegate-less, stat-less result is a class-item masterwork; the k-th one in table order
    // belongs to the k-th class. It only counts when no linked group fits the piece.
    std::size_t classItemResult = set.count;
    std::size_t delegateless = 0;
    for (std::size_t index = 0; index < set.count; ++index) {
        const items::Definition& result = set.results[index];
        std::uint8_t lane = 0;
        if (result.linkedPlugIndex != items::kUnavailableLinkedPlugIndex) {
            if (result.definitionHash != currentPlugHash && group_lane(set, index, target, lane)) {
                pick[pickCount] = index;
                pickLanes[pickCount++] = lane;
            }
            continue;
        }
        item_details::Definition detail{};
        if (detail_of(result, detail) && detail.statCount != 0) {
            if (result.definitionHash != currentPlugHash && plug_fits_target(detail, target)) {
                pick[pickCount] = index;
                pickLanes[pickCount++] = 0;
            }
            continue;
        }
        if (set.linking && delegateless++ == characterClass
            && result.definitionHash != currentPlugHash) {
            classItemResult = index;
        }
    }
    if (pickCount == 0 && classItemResult < set.count) {
        pick[pickCount] = classItemResult;
        pickLanes[pickCount++] = 0;
    }
    if (pickCount == 0) {
        return false;
    }
    const std::size_t chosen = static_cast<std::size_t>(mix(seed) % pickCount);
    const items::Definition& result = set.results[pick[chosen]];
    rolled.plugHash = result.definitionHash;
    return resolve_link(result, pickLanes[chosen], rolled);
}

/** Re-derives the linked perk for a result an earlier staging rolled. */
bool pin_rolled_plug(std::uint32_t plugHash,
                     const item_details::Definition& target,
                     RolledPlug& rolled) noexcept {
    rolled = {};
    items::Definition result{};
    if (!build_data::find_item_definition_hash(plugHash, result)
        || result.definitionHash != plugHash || !items::rolled_result(result)) {
        return false;
    }
    rolled.plugHash = plugHash;
    if (result.linkedPlugIndex == items::kUnavailableLinkedPlugIndex) {
        return true;
    }
    ResultSet set{};
    if (!collect_results(result.plugCategoryHash, set)) {
        return false;
    }
    std::uint8_t lane = 0;
    for (std::size_t index = 0; index < set.count; ++index) {
        if (set.results[index].definitionHash == plugHash) {
            return group_lane(set, index, target, lane) && resolve_link(result, lane, rolled);
        }
    }
    return false;
}

} // namespace sunrise::state::runtime::detail
