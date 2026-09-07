#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::activity_host_manager::response {

/** Fixed opaque activity-data extent carried after the allocated session id. */
inline constexpr std::size_t kActivityDataSize = 128;
/** Exact service-7 body extent. */
inline constexpr std::size_t kResponseBodySize = 1 + sizeof(std::uint64_t) + kActivityDataSize;

/** Complete decoded service-7 response, preserving the opaque activity data losslessly. */
struct Response final {
    std::uint64_t sessionId{};
    std::array<std::byte, kActivityDataSize> activityData{};
};

/** Decodes one exact service-7 response and preserves all 128 opaque bytes. */
[[nodiscard]] bool decode_response(std::span<const std::byte> input, Response& response) noexcept;

/** Encodes one complete service-7 response from a nonzero session and opaque activity data. */
[[nodiscard]] bool encode_response(const Response& response,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

/**
 * Encodes a service-7 response with the compatibility zero activity-data policy.
 * @param sessionId Activity-host session id. Must not be zero.
 * @param output Caller-owned response storage, unchanged on failure.
 * @param written Receives the response size, or zero on failure.
 * @return True when the session id is valid and the whole body fits.
 */
[[nodiscard]] bool encode_response(std::uint64_t sessionId,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace sunrise::middleware::bap::activity_host_manager::response
