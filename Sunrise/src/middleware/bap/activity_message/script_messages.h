#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::activity_message::script_messages {

/** Message type and schema the client accepts for the script-state table. */
inline constexpr std::uint32_t kStateMessageType = 40;
inline constexpr std::uint32_t kStateSchema = 0x80809B27U;
/** The ten-bit record count caps the table, and the blob keeps the server-side ceiling. */
inline constexpr std::size_t kStateMaximumRecords = 512;
inline constexpr std::size_t kStateMaximumBlobBytes = 8'000;
inline constexpr std::size_t kStateCountBits = 10;
inline constexpr std::size_t kStateRecordBits = 128;
inline constexpr std::size_t kStateBlobLengthBits = 32;
/** Body sizes the encoder buffer must cover, empty and full. */
inline constexpr std::size_t kStateMinimumBits = kStateCountBits + kStateBlobLengthBits;
inline constexpr std::size_t kStateMaximumBits =
    kStateCountBits + kStateMaximumRecords * kStateRecordBits + kStateBlobLengthBits
    + kStateMaximumBlobBytes * 8U;
/** Same size padded up to whole bytes. */
inline constexpr std::size_t kStateMaximumBytes = (kStateMaximumBits + 7U) / 8U;

/** One fixed script-state record. Hash identities and offset target semantics remain unnamed. */
struct StateRecord final {
    std::uint32_t hashA{};
    std::uint32_t hashB{};
    std::uint32_t hashC{};
    std::int32_t stringOffset{};
};

/** Complete bounded message-40 body, including its raw tail outside reflection. */
struct StateBody final {
    std::array<StateRecord, kStateMaximumRecords> records{};
    std::array<std::byte, kStateMaximumBlobBytes> blob{};
    std::uint16_t recordCount{};
    std::uint32_t blobLength{};
};

/** Computes the meaningful bit count for one bounded script-state body. */
[[nodiscard]] bool
state_bit_count(std::size_t records, std::size_t blobBytes, std::size_t& bits) noexcept;

/** Computes the padded byte extent for one bounded script-state body. */
[[nodiscard]] bool
state_byte_count(std::size_t records, std::size_t blobBytes, std::size_t& bytes) noexcept;

/** Encodes one exact message-40 reflected table and raw blob. */
[[nodiscard]] bool
encode_state(const StateBody& body, std::span<std::byte> output, std::size_t& written) noexcept;

/** Decodes one exact message-40 body with server-side count clamps. */
[[nodiscard]] bool decode_state(std::span<const std::byte> input, StateBody& body) noexcept;

/** Checks one exact message-40 body without retaining decoded state. */
[[nodiscard]] bool validate_state(std::span<const std::byte> input) noexcept;

/** Message type and schema the client accepts for one script event. */
inline constexpr std::uint32_t kEventMessageType = 41;
inline constexpr std::uint32_t kEventSchema = 0x8080866CU;
/** The nine-bit payload length caps the event tail, and the header is fixed width. */
inline constexpr std::size_t kEventMaximumPayloadBytes = 256;
inline constexpr std::size_t kEventHeaderBits = 107;
/** Body size the encoder buffer must cover at the full payload length. */
inline constexpr std::size_t kEventMaximumBits = kEventHeaderBits + kEventMaximumPayloadBytes * 8U;
inline constexpr std::size_t kEventMaximumBytes = (kEventMaximumBits + 7U) / 8U;

/** Complete bounded message-41 event record. Hash and kind meanings remain unnamed. */
struct EventBody final {
    std::uint32_t hashA{};
    std::uint32_t hashB{};
    std::uint32_t hashC{};
    std::int8_t kind{-1};
    std::array<std::int8_t, kEventMaximumPayloadBytes> payload{};
    std::uint16_t payloadLength{};
};

/** Computes the meaningful bit count for one bounded script-event body. */
[[nodiscard]] bool event_bit_count(std::size_t payloadBytes, std::size_t& bits) noexcept;

/** Computes the padded byte extent for one bounded script-event body. */
[[nodiscard]] bool event_byte_count(std::size_t payloadBytes, std::size_t& bytes) noexcept;

/** Encodes one exact message-41 event record. */
[[nodiscard]] bool
encode_event(const EventBody& body, std::span<std::byte> output, std::size_t& written) noexcept;

/** Decodes one exact message-41 body with the declared array clamp. */
[[nodiscard]] bool decode_event(std::span<const std::byte> input, EventBody& body) noexcept;

/** Checks one exact message-41 body without retaining decoded state. */
[[nodiscard]] bool validate_event(std::span<const std::byte> input) noexcept;

} // namespace sunrise::middleware::bap::activity_message::script_messages
