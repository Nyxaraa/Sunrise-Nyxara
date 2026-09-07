#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../internal.h"
#include "definition.h"

namespace sunrise::server::bap::encrypted::queuez {

/** @return True when an acquisition or reward transaction owns this outcome. */
[[nodiscard]] bool owns_reward_outcome(const ServiceOutcome& outcome) noexcept;

/**
 * Stages the queuez frames one acquisition or reward outcome asks for.
 * @param scratch Transform buffers owned by the lock.
 * @param before Queuez state the current BAP peer can see.
 * @param outcome Outcome the reward lane owns.
 * @param presentationRows Item identities already pinned to feed-referenced rows.
 * @param key Active AES-GCM session key.
 * @param nonce Local send nonce, advanced only by whole staged frames.
 * @param response Whole-frame staging storage owned by the lock.
 * @param written Bytes already staged, updated only by whole frames.
 * @param after Receives the peer after-image the staged frames promise.
 * @return False when a frame could not be built, so State must not commit.
 */
[[nodiscard]] bool
stage_reward_outcome(Scratch& scratch,
                     const SessionState& before,
                     const ServiceOutcome& outcome,
                     std::span<const AcquisitionPresentationRow> presentationRows,
                     std::span<const std::byte, state::kAesKeySize> key,
                     std::array<std::byte, state::kBapNonceSize>& nonce,
                     std::span<std::byte> response,
                     std::size_t& written,
                     SessionState& after) noexcept;

} // namespace sunrise::server::bap::encrypted::queuez
