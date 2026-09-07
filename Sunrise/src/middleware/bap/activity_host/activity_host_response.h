#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::activity_host {

/** Exact service-16 host-lookup request extent. */
inline constexpr std::size_t kRequestBodySize = sizeof(std::uint64_t);
/** Exact service-17 relay response extent. */
inline constexpr std::size_t kResponseBodySize =
    sizeof(std::uint64_t) + sizeof(std::uint32_t) + 2 * sizeof(std::uint16_t);

/** Complete service-16 request. A zero opaque identity is still structurally valid. */
struct Request final {
    std::uint64_t activityHostId{};
};

/** Service-17 response. The middle 16-bit field must carry the neutral value or it is refused. */
struct Response final {
    std::uint64_t activityHostId{};
    std::uint32_t relayAddress{};
    std::uint16_t neutral{};
    std::uint16_t relayPort{};
};

/** Decodes one exact service-16 request. */
[[nodiscard]] bool decode_request(std::span<const std::byte> input, Request& request) noexcept;

/** Encodes one exact service-16 request. */
[[nodiscard]] bool
encode_request(const Request& request, std::span<std::byte> output, std::size_t& written) noexcept;

/** Decodes one exact service-17 response and validates its endpoint and neutral field. */
[[nodiscard]] bool decode_response(std::span<const std::byte> input, Response& response) noexcept;

/** Encodes one complete validated service-17 response. */
[[nodiscard]] bool encode_response(const Response& response,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

/**
 * Checks one svc-16 host identity and encodes its svc-17 relay endpoint.
 * @param requestBody Complete fixed-width request body.
 * @param relayAddress Relay IPv4 address in host order. Must not be zero.
 * @param relayPort Relay port in host order. Must not be zero.
 * @param output Caller-owned response-body storage, unchanged on failure.
 * @param written Receives the response size, or zero on failure.
 * @return True when the request and the relay endpoint are valid and fit.
 */
[[nodiscard]] bool encode_response(std::span<const std::byte> requestBody,
                                   std::uint32_t relayAddress,
                                   std::uint16_t relayPort,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace sunrise::middleware::bap::activity_host
