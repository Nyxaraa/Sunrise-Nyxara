#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "../../../../state/account/account_state.h"

namespace sunrise::state::record_claims {
struct PendingClaim;
}

namespace sunrise::middleware::datagen::family4::account {

/**
 * Encodes a sentinel-correct account object from authored State.
 * @param state Account identity, roster, preferences, and selected-character state.
 * @param output Exact State-mapped account-object storage.
 * @return True when State is valid and every required fixed region fits.
 */
[[nodiscard]] bool
encode(const state::AccountState& state,
       std::span<std::byte> output,
       std::optional<std::uint16_t> pendingSeasonReward = std::nullopt,
       const state::record_claims::PendingClaim* pendingRecordClaim = nullptr) noexcept;

} // namespace sunrise::middleware::datagen::family4::account
