#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../../../internal.h"

namespace sunrise::server::bap::encrypted::push::activity {

/** Appends one pending msg-30 query on a local nonce transaction. */
[[nodiscard]] bool
append_authority_query_notification(Session& session,
                                    Scratch& scratch,
                                    std::span<const std::byte, state::kAesKeySize> key,
                                    std::array<std::byte, state::kBapNonceSize>& nonce,
                                    std::span<std::byte> response,
                                    std::size_t& written) noexcept;

/** Returns a staged msg-30 to pending when its frame is refused. */
void discard_staged_authority_query(Session& session) noexcept;

/** Starts the answer timeout after a staged msg-30 reaches the caller. */
void commit_staged_authority_query(Session& session, std::uint64_t now) noexcept;

} // namespace sunrise::server::bap::encrypted::push::activity
