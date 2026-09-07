#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::middleware::gameplay::descriptor {

/** The join descriptor is exactly 128 bytes, and a different count makes it absent. */
inline constexpr std::size_t kDescriptorSize = 128;

/** A peer NetAddr is exactly 86 bytes wherever it appears. */
inline constexpr std::size_t kNetAddrSize = 86;

/** Everything one direct-path join descriptor publishes. */
struct JoinEndpoint {
    /** Nonzero machine identity. A zero identity makes the descriptor read as absent. */
    std::uint64_t machineId{};
    /** Advertised IPv4 in host order. */
    std::uint32_t address{};
    /** Advertised UDP port in host order. It must be even. */
    std::uint16_t port{};
    /** Nonzero online session id. Its low 64 bits are the lobby the client joins. */
    std::uint64_t onlineSessionId{};
};

/**
 * Builds one NetAddr for the direct method-0 path.
 * The endpoint goes in twice, as local entry 0 and as the public entry. A peer with no public
 * entry is unroutable and the client falls back to NAT traversal.
 * @param address IPv4 in host order.
 * @param port UDP port in host order.
 * @param output Receives all 86 bytes.
 */
void write_net_addr(std::uint32_t address,
                    std::uint16_t port,
                    std::array<std::byte, kNetAddrSize>& output) noexcept;

/**
 * Builds one NetAddr in the direct form the client's direct security manager decodes.
 * The fields are the `DRCT` magic, the IPv4, a zero address id and the port, high byte first.
 * @param address IPv4 in host order.
 * @param port UDP port in host order.
 * @param output Receives all 86 bytes.
 */
void write_direct_net_addr(std::uint32_t address,
                           std::uint16_t port,
                           std::array<std::byte, kNetAddrSize>& output) noexcept;

/**
 * Builds one join descriptor for the direct method-0 path.
 * @param endpoint Advertised identity and endpoint.
 * @param output Receives all 128 bytes only on success.
 * @return True when the identity, address, port, and session id are all usable.
 */
[[nodiscard]] bool build(const JoinEndpoint& endpoint,
                         std::array<std::byte, kDescriptorSize>& output) noexcept;

} // namespace sunrise::middleware::gameplay::descriptor
