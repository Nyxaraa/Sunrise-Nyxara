#pragma once

#include <cstdint>

#include "account_state.h"

namespace sunrise::state::account {

/**
 * Reports whether an item is a pursuit the selected character already holds.
 *
 * A pursuit - quest, bounty, token - is unique per character. Two kinds of item are deliberately
 * not pursuits, because holding several of each is legitimate: gear, told apart by carrying an
 * equipment slot, and consumables and materials, told apart by declaring a stack larger than one.
 * The stack size is what separates the second group, not the instanced state: a pursuit carries no
 * instance data either, so it is marked stackable exactly as a consumable is.
 *
 * This lives in State because it mirrors the classification the client's native vendor-row gate
 * applies locally when deciding whether a row is still offered, and the two must not drift.
 *
 * @param itemDefinitionIndex Item to classify.
 * @return True when this is a pursuit the selected character already holds.
 */
[[nodiscard]] bool holds_pursuit(std::uint16_t itemDefinitionIndex) noexcept;

/**
 * The same rule, against an account view the caller already holds.
 *
 * Reading the account copies the whole of it, so a walk over many candidates - a bounty roll tests
 * every item in the vendor's pool - takes one view and reuses it rather than copying per candidate.
 *
 * @param account Account view to test against.
 * @param itemDefinitionIndex Item to classify.
 * @return True when this is a pursuit that view's selected character already holds.
 */
[[nodiscard]] bool holds_pursuit(const AccountState& account,
                                 std::uint16_t itemDefinitionIndex) noexcept;

} // namespace sunrise::state::account
