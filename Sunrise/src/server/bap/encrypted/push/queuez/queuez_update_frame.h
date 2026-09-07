#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "../../../../../middleware/queuez/queuez_update.h"
#include "../../internal.h"
#include "../snapshot/snapshot.h"

namespace sunrise::server::bap::encrypted::push::queuez_frame {

/** Securely clears raw and compressed object-staging prefixes. */
void clear_object_storage(Scratch& scratch,
                          std::size_t rawClearSize,
                          std::size_t compressedClearSize) noexcept;

/**
 * Appends one prepared family as a complete authenticated svc-123 frame.
 * @param scratch Lock-owned raw, body, payload, and sealed storage.
 * @param family Prepared family whose borrowed payloads stay valid through update encoding.
 * @param rawClearSize Plaintext prefix borrowed by raw or patch payloads.
 * @param compressedClearSize Sealed prefix borrowed by compressed object payloads.
 * @param key Active AES-GCM session key.
 * @param nonce Push-direction nonce after any correlated response.
 * @param response Caller-owned output containing prior complete frames.
 * @param written Existing byte count, updated only when the complete push fits.
 * @return True when update encoding, encryption, and outer framing all succeed.
 */
[[nodiscard]] bool append(Scratch& scratch,
                          const middleware::queuez::Family& family,
                          std::size_t rawClearSize,
                          std::size_t compressedClearSize,
                          std::span<const std::byte, state::kAesKeySize> key,
                          std::span<const std::byte, state::kBapNonceSize> nonce,
                          std::span<std::byte> response,
                          std::size_t& written) noexcept;

/**
 * Appends one prepared snapshot as a complete frame, unpacking its family and clear extents.
 * @param scratch Lock-owned raw, body, payload, and sealed storage.
 * @param prepared Snapshot whose borrowed payloads stay valid through update encoding.
 * @param key Active AES-GCM session key.
 * @param nonce Push-direction nonce after any correlated response.
 * @param response Caller-owned output containing prior complete frames.
 * @param written Existing byte count, updated only when the complete push fits.
 * @return True when update encoding, encryption, and outer framing all succeed.
 */
[[nodiscard]] bool append_prepared(Scratch& scratch,
                                   const snapshot::Prepared& prepared,
                                   std::span<const std::byte, state::kAesKeySize> key,
                                   std::span<const std::byte, state::kBapNonceSize> nonce,
                                   std::span<std::byte> response,
                                   std::size_t& written) noexcept;

/** Appends one prepared snapshot and advances the nonce only when the whole frame fits. */
[[nodiscard]] bool append_prepared_frame(Scratch& scratch,
                                         const snapshot::Prepared& prepared,
                                         std::span<const std::byte, state::kAesKeySize> key,
                                         std::array<std::byte, state::kBapNonceSize>& nonce,
                                         std::span<std::byte> response,
                                         std::size_t& written) noexcept;

} // namespace sunrise::server::bap::encrypted::push::queuez_frame
