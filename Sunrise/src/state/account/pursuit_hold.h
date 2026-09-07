#pragma once

#include <cstdint>

#include "account_state.h"

namespace sunrise::state::account {

/**
 * Reports whether an item is a pursuit the selected character already holds.
 * A pursuit is unique per character. Gear carries an equipment slot and consumables declare a
 * stack larger than one, so neither is one. Must match the client's own vendor-row gate.
 * @param itemDefinitionIndex Item to classify.
 * @return True when this is a pursuit the selected character already holds.
 */
[[nodiscard]] bool holds_pursuit(std::uint16_t itemDefinitionIndex) noexcept;

/**
 * The same rule, against an account view the caller already holds.
 * Reading the account copies all of it, so a walk over many candidates reuses one view.
 * @param account Account view to test against.
 * @param itemDefinitionIndex Item to classify.
 * @return True when this is a pursuit that view's selected character already holds.
 */
[[nodiscard]] bool holds_pursuit(const AccountState& account,
                                 std::uint16_t itemDefinitionIndex) noexcept;

} // namespace sunrise::state::account
