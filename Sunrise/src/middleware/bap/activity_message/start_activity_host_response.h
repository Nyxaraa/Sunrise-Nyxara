#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::activity_message::start_activity_host_response {

/** Message type the client accepts, and the fixed field sizes of its body. */
inline constexpr std::uint32_t kMessageType = 10;
inline constexpr std::size_t kSessionIdBytes = 8;
inline constexpr std::size_t kHostInstanceNameBytes = 128;
inline constexpr std::size_t kByteCount = kSessionIdBytes + kHostInstanceNameBytes;

/** Exact fixed message-10 body. The 128-byte tail is preserved without interpretation. */
struct Body final {
    std::uint64_t sessionId{};
    std::array<std::byte, kHostInstanceNameBytes> hostInstanceName{};
};

/** Encodes one nonzero session id and the complete 128-byte instance-name field. */
[[nodiscard]] bool
encode(const Body& body, std::span<std::byte> output, std::size_t& written) noexcept;

/** Decodes one exact 136-byte body and rejects a zero session id. */
[[nodiscard]] bool decode(std::span<const std::byte> input, Body& body) noexcept;

/** Checks one exact message-10 body without retaining decoded state. */
[[nodiscard]] bool validate(std::span<const std::byte> input) noexcept;

} // namespace sunrise::middleware::bap::activity_message::start_activity_host_response
