#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::activity_message::replication_epoch {

/** Activity message 44 is the host-to-client replication-epoch control body. */
inline constexpr std::uint32_t kMessageType = 44;
/** Activity message 44 carries one generation byte and one abort bit. */
inline constexpr std::size_t kBitCount = 9;
inline constexpr std::size_t kEncodedSize = 2;

/** Complete message-44 body. The sender refuses a set abort bit. */
struct Body final {
    std::uint8_t epoch{};
    bool abort{};
};

/**
 * Encodes activity message 44 with the abort bit forced to zero.
 * A set abort bit permanently kills the client's roster path, so nothing else may build this body.
 * TODO: no sender yet. The common-generation barrier has to be measured before this is called.
 */
[[nodiscard]] bool
encode(std::uint8_t generation, std::span<std::byte> output, std::size_t& written) noexcept;

/** Encodes one complete safe message-44 body without changing a refused output buffer. */
[[nodiscard]] bool
encode_body(const Body& body, std::span<std::byte> output, std::size_t& written) noexcept;

/** Decodes one exact message-44 body and checks its seven zero padding bits. */
[[nodiscard]] bool decode(std::span<const std::byte> input, Body& body) noexcept;

/** @return True when the input is one complete syntactically valid message-44 body. */
[[nodiscard]] bool validate(std::span<const std::byte> input) noexcept;

} // namespace sunrise::middleware::bap::activity_message::replication_epoch
