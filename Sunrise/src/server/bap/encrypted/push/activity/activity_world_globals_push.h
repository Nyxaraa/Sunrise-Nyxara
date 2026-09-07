#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../../../state/activity/definition.h"
#include "../../../internal.h"

namespace sunrise::server::bap::encrypted::push::activity {

/**
 * Appends one world-globals-state svc9 notification and advances its local nonce.
 * The body carries the activity-clock enable. Only the sign of its real32 is read, and a zero or
 * negative value freezes the client's activity clock at its base, so this sends a fixed positive.
 * @param scratch Lock-owned transform buffers.
 * @param sessionId Activity session echoed in the svc9 envelope.
 * @param key Active AES-GCM session key.
 * @param nonce Local send nonce advanced only after the complete notification exists.
 * @param response Lock-owned complete-frame staging storage.
 * @param written Existing staged byte count, updated only after the notification exists.
 * @return True when the notification encodes atomically.
 */
[[nodiscard]] bool
append_world_globals_notification(Scratch& scratch,
                                  std::uint64_t sessionId,
                                  std::span<const std::byte, state::kAesKeySize> key,
                                  std::array<std::byte, state::kBapNonceSize>& nonce,
                                  std::span<std::byte> response,
                                  std::size_t& written) noexcept;

} // namespace sunrise::server::bap::encrypted::push::activity
