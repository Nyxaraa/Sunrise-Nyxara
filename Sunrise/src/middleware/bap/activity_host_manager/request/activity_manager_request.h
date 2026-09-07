#pragma once

#include <cstddef>
#include <span>

#include "../definition.h"

namespace sunrise::middleware::bap::activity_host_manager::request {

/** One discriminator and one little-endian length precede the reserved payload region. */
inline constexpr std::size_t kRequestPrefixSize = 5;
/** Fixed producer capacity shared by declared protobuf and reserved padding. */
inline constexpr std::size_t kProtobufCapacity = 7'714;
/** Exact service-6 body extent. */
inline constexpr std::size_t kRequestBodySize = kRequestPrefixSize + kProtobufCapacity;

/**
 * Checks one service-6 body and borrows only its declared protobuf bytes. The borrowed bytes
 * can hold credentials and must not outlive the input.
 * @param input Complete decrypted request body.
 * @param request Cleared first. Receives a borrowed view only on success.
 * @return True when the fixed envelope and the declared protobuf length are valid.
 */
[[nodiscard]] bool parse_request(std::span<const std::byte> input, Request& request) noexcept;

/**
 * Re-encodes one parsed request, preserving every reserved padding byte.
 * The borrowed views can contain credentials and are consumed only during this call.
 * @param request Declared protobuf and the exact remaining reserved region.
 * @param output Caller-owned storage, unchanged on failure.
 * @param written Receives 7,719 on success or zero on failure.
 */
[[nodiscard]] bool
encode_request(const Request& request, std::span<std::byte> output, std::size_t& written) noexcept;

/**
 * Encodes one request with a zero-filled reserved tail.
 * @param protobuf Declared protobuf bytes, at most 7,714.
 * @param output Caller-owned storage, unchanged on failure.
 * @param written Receives 7,719 on success or zero on failure.
 */
[[nodiscard]] bool encode_request(std::span<const std::byte> protobuf,
                                  std::span<std::byte> output,
                                  std::size_t& written) noexcept;

} // namespace sunrise::middleware::bap::activity_host_manager::request
