#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../../../internal.h"
#include "../../activity_message/definition.h"

namespace sunrise::server::bap::encrypted::push::activity {

/**
 * Appends one current membership svc9 notification and advances its local nonce.
 * @param scratch Lock-owned transform buffers.
 * @param activity Session and prepared State snapshot picked by the svc8 route.
 * @param key Active AES-GCM session key.
 * @param nonce Local send nonce advanced only after the complete notification exists.
 * @param response Lock-owned complete-frame staging storage.
 * @param written Existing staged byte count, updated only after the notification exists.
 * @param suppressUnchanged True to skip a body byte-identical to the last delivered one; the
 * client drops same-revision repeats, so a byte-identical repeat may pass true.
 * @param suppressed Set true when the body was skipped as an already-delivered repeat.
 * @return True when the snapshot and notification encode atomically.
 */
[[nodiscard]] bool
append_membership_notification(Scratch& scratch,
                               Session& session,
                               const activity_message::ActivityPlan& activity,
                               std::span<const std::byte, state::kAesKeySize> key,
                               std::array<std::byte, state::kBapNonceSize>& nonce,
                               std::span<std::byte> response,
                               std::size_t& written,
                               bool suppressUnchanged = false,
                               bool* suppressed = nullptr) noexcept;

/**
 * Appends the membership body a public-target join answers, framed against the joining session.
 * The connection still holds its previous binding while a join stages, so this frames against the
 * plan. It carries no citizen advertisement, which only a private link publishes.
 * @param scratch Lock-owned transform buffers.
 * @param session Connection whose selected activity gives the authored region policy.
 * @param activity Joining session and the member table read for it.
 * @param key Active AES-GCM session key.
 * @param nonce Local send nonce advanced only after the complete notification exists.
 * @param response Lock-owned complete-frame staging storage.
 * @param written Existing staged byte count, updated only after the notification exists.
 * @return True when the snapshot and notification encode atomically.
 */
[[nodiscard]] bool
append_join_membership_notification(Scratch& scratch,
                                    const Session& session,
                                    const activity_message::ActivityPlan& activity,
                                    std::span<const std::byte, state::kAesKeySize> key,
                                    std::array<std::byte, state::kBapNonceSize>& nonce,
                                    std::span<std::byte> response,
                                    std::size_t& written) noexcept;

} // namespace sunrise::server::bap::encrypted::push::activity
