#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::gameplay::association {

/** Identity fields are raw 64-bit values. */
inline constexpr std::size_t kIdentitySize = 8;
/** The connection nonce seed is a raw 96-bit value. */
inline constexpr std::size_t kSeedSize = 12;
/** Key material fields are raw 1024-bit values. */
inline constexpr std::size_t kIntegerSize = 128;
/** Direct IPv4 appends a two-byte clear address trailer after the control bits. */
inline constexpr std::size_t kTrailerSize = 2;
/** One control datagram never exceeds this, so it fits fixed endpoint storage. */
inline constexpr std::size_t kControlCapacity = 512;

/** Connection-control opcodes carried in the three bits after both association words. */
enum class Opcode : std::uint8_t {
    connectRequest = 0,
    keyExchangeOffer = 1,
    keyExchangeResponse = 2,
    reject = 3,
    keepAlive = 4,
    invalid = 5,
};

/** One decoded or encodable key-exchange control packet. */
struct ControlPacket {
    /** Sender's association word A. An offer advances it on every retry. */
    std::uint32_t wordA{};
    /** Sender's association word B. It is stable across the retries of one attempt. */
    std::uint32_t wordB{};
    Opcode opcode{Opcode::invalid};
    std::array<std::byte, kIdentitySize> networkId{};
    std::array<std::byte, kIdentitySize> identityNonce{};
    /** Identity public field. The exchange consumes it as the verifier. */
    std::array<std::byte, kIntegerSize> verifier{};
    bool hasSignOnBlob{};
    /** Relayed SignOn material. Nothing in the exchange reads it. */
    std::array<std::byte, kIntegerSize> signOnBlob{};
    /** Sender's locally generated nonce seed. */
    std::array<std::byte, kSeedSize> seed{};
    /** Sender's public value. */
    std::array<std::byte, kIntegerSize> publicValue{};
    /** Echo of the offer's word B. Present on the response only. */
    std::uint32_t responseValue{};
    /** Clear direct-address trailer retained byte for byte across the response. */
    std::array<std::byte, kTrailerSize> trailer{};
};

/**
 * Decodes one received control datagram.
 * The clear address trailer is stripped first. Both association words start at bit zero.
 * @param datagram Whole received payload.
 * @param output Receives the packet only when every field of the opcode was present.
 * @return True for a complete offer or response. Other opcodes decode their header only.
 */
[[nodiscard]] bool decode(std::span<const std::byte> datagram, ControlPacket& output) noexcept;

/**
 * Encodes one control datagram.
 * @param packet Fields to write. The opcode selects which of them are read.
 * @param output Caller storage of at least kControlCapacity bytes.
 * @param written Receives the datagram length on success.
 * @return True when the whole packet and its trailer fit the output.
 */
[[nodiscard]] bool
encode(const ControlPacket& packet, std::span<std::byte> output, std::size_t& written) noexcept;

} // namespace sunrise::middleware::gameplay::association
