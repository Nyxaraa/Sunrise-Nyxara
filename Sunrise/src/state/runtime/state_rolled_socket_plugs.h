#pragma once

#include <cstddef>
#include <cstdint>

#include "../build_data/items/details/definition.h"
#include "../build_data/items/item_catalog.h"

namespace sunrise::state::runtime::detail {

/**
 * Some sockets offer only action plugs, which carry no effect of their own. The service answered
 * them with a result plug from a roll set: result plugs sit outside the socket's own pool but each
 * names its set and shares the action plugs' category. All of it is derived from the item table.
 */

/** How one requested plug should be socketed. */
enum class RolledPlugAction : std::uint8_t {
    /** An ordinary plug; socket it as asked. */
    none,
    /** An action plug of a rolled socket; roll a result plug and socket that instead. */
    roll,
};

/**
 * Classifies one requested plug for one target lane.
 * @param requested Installed definition of the plug the Client asked to socket.
 * @param target Installed definition of the item being socketed.
 * @param lane Ordinary socket lane the request names.
 * @return roll when the plug is stat-less, pooled for that lane, and its category has result
 *         plugs the pool does not offer.
 */
[[nodiscard]] RolledPlugAction classify_rolled_plug(const build_data::items::Definition& requested,
                                                    const build_data::items::Definition& target,
                                                    std::uint8_t lane) noexcept;

/**
 * @return True when a plug already in a lane is a rolled result of any set, which is what a
 *         re-roll replaces and what marks the item as masterworked.
 */
[[nodiscard]] bool is_rolled_result(std::uint32_t plugHash) noexcept;

/** One roll outcome: the result plug and, for a delegating result, the perk it stands for. */
struct RolledPlug {
    std::uint32_t plugHash{};
    /** Lane whose pool offers the linked perk; only meaningful when linkedPerkHash is nonzero. */
    std::uint8_t linkedLane{};
    /** Stat perk the result stands for, which the service swapped into linkedLane; 0 for none. */
    std::uint32_t linkedPerkHash{};
};

/**
 * Rolls one result plug for a target item. A stat-carrying result needs every stat it contributes
 * declared by the target; a delegating result needs the target's pools to offer a perk of its
 * group, else the delegate-less result for the class. The current plug is never rolled again.
 * @param requested The action plug, which names the roll set through its category.
 * @param target Installed detail of the item being socketed.
 * @param characterClass Wire class of the owning character.
 * @param currentPlugHash Plug already in the lane, or 0.
 * @param seed Any per-request entropy.
 * @param rolled Receives the outcome.
 * @return True when at least one eligible result existed.
 */
[[nodiscard]] bool roll_socket_plug(const build_data::items::Definition& requested,
                                    const build_data::items::details::Definition& target,
                                    std::uint8_t characterClass,
                                    std::uint32_t currentPlugHash,
                                    std::uint64_t seed,
                                    RolledPlug& rolled) noexcept;

/**
 * Re-derives the linked perk for a result an earlier staging rolled, so a re-staging lands on
 * the same after-image.
 * @param plugHash Result plug the earlier staging chose.
 * @param target Installed detail of the item being socketed.
 * @param rolled Receives the plug and its linked perk, if it has one.
 * @return True when the plug is a rolled result the target could have rolled.
 */
[[nodiscard]] bool pin_rolled_plug(std::uint32_t plugHash,
                                   const build_data::items::details::Definition& target,
                                   RolledPlug& rolled) noexcept;

} // namespace sunrise::state::runtime::detail
