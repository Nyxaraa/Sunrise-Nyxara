#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../../../internal.h"

namespace sunrise::server::bap::encrypted::push::activity {

/** Appends one pending msg-28 reset on a local nonce transaction. */
[[nodiscard]] bool
append_authority_reset_notification(Session& session,
                                    Scratch& scratch,
                                    std::span<const std::byte, state::kAesKeySize> key,
                                    std::array<std::byte, state::kBapNonceSize>& nonce,
                                    std::span<std::byte> response,
                                    std::size_t& written) noexcept;

/** Returns a staged msg-28 to pending when its frame is refused. */
void discard_staged_authority_reset(Session& session) noexcept;

/** Starts the acknowledgement timeout after a staged msg-28 reaches the caller. */
void commit_staged_authority_reset(Session& session, std::uint64_t now) noexcept;

} // namespace sunrise::server::bap::encrypted::push::activity
