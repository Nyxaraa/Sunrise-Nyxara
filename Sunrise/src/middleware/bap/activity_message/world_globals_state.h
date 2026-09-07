#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::activity_message::world_globals_state {

/** Message type and schema the client accepts, and the fixed size of its body. */
inline constexpr std::uint32_t kMessageType = 2;
inline constexpr std::uint32_t kSchema = 0x8080867EU;
inline constexpr std::size_t kBitCount = 33;
inline constexpr std::size_t kByteCount = 5;

/** Complete fixed message-2 body, with the real32 retained as raw IEEE-754 bits. */
struct Body final {
    bool flag{};
    std::uint32_t real32Bits{};
};

/** Encodes the exact bool followed by the unaligned 32-bit real value. */
[[nodiscard]] bool
encode(const Body& body, std::span<std::byte> output, std::size_t& written) noexcept;

/** Decodes one exact five-byte body and rejects nonzero trailing padding. */
[[nodiscard]] bool decode(std::span<const std::byte> input, Body& body) noexcept;

/** Checks one exact message-2 body without retaining its decoded state. */
[[nodiscard]] bool validate(std::span<const std::byte> input) noexcept;

} // namespace sunrise::middleware::bap::activity_message::world_globals_state
