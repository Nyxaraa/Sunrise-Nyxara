#include "join_descriptor.h"

#include <algorithm>
#include <span>

namespace sunrise::middleware::gameplay::descriptor {

namespace {

/** The machine identity occupies the first eight bytes. */
constexpr std::size_t kMachineOffset = 0;
/** The NetAddr follows the machine identity. */
constexpr std::size_t kNetAddrOffset = 8;
/** The IPv4 address opens the first local entry, in network byte order. */
constexpr std::size_t kAddressOffset = 0;
/** The UDP port follows the address, low byte first. The IPv4 beside it stays network order. */
constexpr std::size_t kPortOffset = 4;
/** The public entry follows five 6-byte local entries, and carries the same two fields. */
constexpr std::size_t kPublicAddressOffset = 30;
/** The public UDP port follows the public address, low byte first. */
constexpr std::size_t kPublicPortOffset = 34;
/** The direct form keeps the IPv4 at 4, a zero address id at 8 and the port at 10. */
constexpr std::size_t kDirectAddressOffset = 4;
constexpr std::size_t kDirectPortOffset = 10;
/** The NAT type closes the address body at offset 40. */
constexpr std::size_t kNatTypeOffset = 40;
/** NAT type 1 reads as open. Zero reads as unknown and leaves the address unroutable. */
constexpr std::byte kNatTypeOpen{1};
/** The transport method is the last NetAddr byte. */
constexpr std::size_t kMethodOffset = kNetAddrSize - 1;
/** Method 0 selects the direct path. Methods 6 and 7 would select the relay instead. */
constexpr std::byte kDirectMethod{0};
/** The online session id occupies the tail, low half first. */
constexpr std::size_t kSessionOffset = 110;
/** Bits in one byte. */
constexpr unsigned kByteBits = 8;
/** Mask of one byte. */
constexpr std::uint64_t kByteMask = 0xFF;
/** The advertised port must be even. */
constexpr std::uint16_t kPortAlignment = 2;

/**
 * Writes one unsigned value in network byte order.
 * @param output Field storage.
 * @param offset First byte of the field.
 * @param value Host-order value.
 * @param width Field width in bytes.
 */
void write_network_order(std::span<std::byte> output,
                         std::size_t offset,
                         std::uint64_t value,
                         std::size_t width) noexcept {
    for (std::size_t index = 0; index < width; ++index) {
        const unsigned shift = static_cast<unsigned>(width - 1 - index) * kByteBits;
        output[offset + index] = static_cast<std::byte>((value >> shift) & kByteMask);
    }
}

/**
 * Writes one unsigned value low byte first.
 * @param output Field storage.
 * @param offset First byte of the field.
 * @param value Host-order value.
 * @param width Field width in bytes.
 */
void write_memory_order(std::span<std::byte> output,
                        std::size_t offset,
                        std::uint64_t value,
                        std::size_t width) noexcept {
    for (std::size_t index = 0; index < width; ++index) {
        const unsigned shift = static_cast<unsigned>(index) * kByteBits;
        output[offset + index] = static_cast<std::byte>((value >> shift) & kByteMask);
    }
}

} // namespace

/** Builds one NetAddr for the direct method-0 path. */
void write_net_addr(std::uint32_t address,
                    std::uint16_t port,
                    std::array<std::byte, kNetAddrSize>& output) noexcept {
    output = {};
    write_network_order(output, kAddressOffset, address, sizeof(std::uint32_t));
    write_memory_order(output, kPortOffset, port, sizeof(std::uint16_t));
    write_network_order(output, kPublicAddressOffset, address, sizeof(std::uint32_t));
    write_memory_order(output, kPublicPortOffset, port, sizeof(std::uint16_t));
    // The 4-byte address id at offset 36 stays zero, as the captured retail descriptor leaves it.
    // A nonzero id made the client stop sending to every private-session region host.
    output[kNatTypeOffset] = kNatTypeOpen;
    output[kMethodOffset] = kDirectMethod;
}

/** Builds one direct-form NetAddr; the client decodes it as one big-endian bitstream. */
void write_direct_net_addr(std::uint32_t address,
                           std::uint16_t port,
                           std::array<std::byte, kNetAddrSize>& output) noexcept {
    output = {};
    /** The client reads these four bytes as the direct-form tag. */
    static constexpr std::array<std::byte, 4> kDirectMagic{
        std::byte{'D'}, std::byte{'R'}, std::byte{'C'}, std::byte{'T'}};
    std::copy(kDirectMagic.begin(), kDirectMagic.end(), output.begin());
    write_network_order(output, kDirectAddressOffset, address, sizeof(std::uint32_t));
    write_network_order(output, kDirectPortOffset, port, sizeof(std::uint16_t));
    output[kMethodOffset] = kDirectMethod;
}

/** Builds one join descriptor for the direct method-0 path. */
bool build(const JoinEndpoint& endpoint, std::array<std::byte, kDescriptorSize>& output) noexcept {
    if (endpoint.machineId == 0 || endpoint.address == 0 || endpoint.port == 0
        || endpoint.port % kPortAlignment != 0 || endpoint.onlineSessionId == 0) {
        return false;
    }
    std::array<std::byte, kNetAddrSize> netAddr{};
    write_net_addr(endpoint.address, endpoint.port, netAddr);

    std::array<std::byte, kDescriptorSize> candidate{};
    write_memory_order(candidate, kMachineOffset, endpoint.machineId, sizeof(std::uint64_t));
    std::copy(netAddr.begin(), netAddr.end(), candidate.begin() + kNetAddrOffset);
    // The join key stays zero. The direct path accepts it and carries no key material.
    write_memory_order(candidate, kSessionOffset, endpoint.onlineSessionId, sizeof(std::uint64_t));
    output = candidate;
    return true;
}

} // namespace sunrise::middleware::gameplay::descriptor
