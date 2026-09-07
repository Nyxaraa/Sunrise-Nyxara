#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../state/gameplay/definition.h"
#include "control_packet.h"

namespace sunrise::middleware::gameplay::association {

/** The envelope keys AES-128, so the derived key contributes its first 16 bytes. */
inline constexpr std::size_t kKeySize = state::gameplay::kChannelKeySize;
/** The nonce is 12 bytes, the same width as the exchanged seed. */
inline constexpr std::size_t kNonceSize = kSeedSize;
/** The envelope carries the full 16-byte tag. */
inline constexpr std::size_t kTagSize = 16;
/** One protected datagram stays inside a single unfragmented transport packet. */
inline constexpr std::size_t kProtectedCapacity = 1500;
/** The clear length fields are 11 bits, so a payload must stay below 2048 bytes. */
inline constexpr std::size_t kMaximumPayload = 2047;

/** Crypto state installed in one step when an association becomes established. */
using ProtectedContext = state::gameplay::ProtectedContext;

/**
 * Seals one payload as a protected datagram.
 * The clear header is not authenticated data: the tag covers the encrypted bits only.
 * @param context Installed crypto state. Its outgoing word A is advanced before the header.
 * @param payload Established transport payload bytes.
 * @param output Caller storage of at least kProtectedCapacity bytes.
 * @param written Receives the datagram length on success.
 * @return True when the payload fit the length fields and the transform succeeded.
 */
[[nodiscard]] bool seal(ProtectedContext& context,
                        std::span<const std::byte> payload,
                        std::span<std::byte> output,
                        std::size_t& written) noexcept;

/**
 * Opens one received protected datagram.
 * No sender state is updated. The caller applies its replay policy after this reports success.
 * @param context Installed crypto state.
 * @param datagram Whole received payload.
 * @param payload Receives the decrypted transport payload.
 * @param payloadSize Receives the payload length.
 * @param wordA Receives the sender's clear word A, which the replay policy consumes.
 * @return True only when the tag authenticates and every declared length is consistent.
 */
[[nodiscard]] bool open(const ProtectedContext& context,
                        std::span<const std::byte> datagram,
                        std::span<std::byte> payload,
                        std::size_t& payloadSize,
                        std::uint32_t& wordA) noexcept;

} // namespace sunrise::middleware::gameplay::association
