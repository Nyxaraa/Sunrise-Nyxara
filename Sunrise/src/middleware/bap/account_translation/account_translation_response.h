#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::account_translation {

/**
 * Reads the one supported svc-23 identity request.
 * @param requestBody Complete request body.
 * @param identity Cleared, then receives the identity; zero is a host peer with no handle.
 * @return True when the request carries at least one type-12 identity.
 */
[[nodiscard]] bool request_identity(std::span<const std::byte> requestBody,
                                    std::uint64_t& identity) noexcept;

/**
 * Encodes the svc-24 answer to one svc-23 identity request. A request that cannot be paired
 * gets a zero-entry body, never a refusal.
 * @param requestBody Complete request body.
 * @param accountSoid Account SOID from Server State.
 * @param output Caller-owned response-body storage, unchanged on failure.
 * @param written Receives the encoded size, or zero on failure.
 * @return True when the paired or the zero-entry answer fits output.
 */
[[nodiscard]] bool encode_response(std::span<const std::byte> requestBody,
                                   std::uint64_t accountSoid,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace sunrise::middleware::bap::account_translation
