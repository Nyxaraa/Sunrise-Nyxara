#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../../../../../state/runtime/state.h"
#include "../../../../activity/host_runtime.h"
#include "../../../internal.h"

namespace sunrise::server::bap::encrypted::push::activity {

/**
 * Appends one pending operator incident and advances only the caller-owned nonce copy.
 * The host output remains pending until commit_staged_incident sees the complete frame copied.
 */
[[nodiscard]] bool
append_incident_notification(Session& session,
                             Scratch& scratch,
                             const server::activity::host::PendingIncident& pending,
                             std::span<const std::byte, state::kAesKeySize> key,
                             std::array<std::byte, state::kBapNonceSize>& nonce,
                             std::span<std::byte> response,
                             std::size_t& written) noexcept;

/** Clears an encoded incident that did not reach the transport caller. */
void discard_staged_incident(Session& session) noexcept;

/** Advances the exact host output after its complete encrypted frame reaches the caller. */
void commit_staged_incident(Session& session) noexcept;

} // namespace sunrise::server::bap::encrypted::push::activity
