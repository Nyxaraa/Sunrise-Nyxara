#include "dtls_messages.h"

#include <algorithm>

namespace sunrise::middleware::gameplay::dtls {

namespace {

/** Bits in one byte. */
constexpr unsigned kByteBits = 8;
/** Mask of one byte. */
constexpr std::uint32_t kByteMask = 0xFF;

/** The message type opens the header. */
constexpr std::size_t kTypeOffset = 0;
/** A constant the requester writes and never reads back. */
constexpr std::size_t kVersionOffset = 1;
/** Value the sender puts in that byte. */
constexpr std::byte kVersion{2};
/** The tag the packet is addressed to follows the version. */
constexpr std::size_t kPeerTagOffset = 2;
/** Four bytes close the header. Both handshake packets leave them zero. */
constexpr std::size_t kReservedOffset = 4;

/** Every verification tag is a 16-bit field. */
constexpr std::size_t kTagBytes = sizeof(std::uint16_t);
/** Tags carried between the cookie and the address: responder, responder, requester, two spare. */
constexpr std::size_t kInitAckTagCount = 5;
/** An IPv4 address travels as four bytes in dotted order. */
constexpr std::size_t kIpv4Bytes = 4;
/** The requester address is the IPv4 bytes and a 16-bit port. */
constexpr std::size_t kAddressBytes = kIpv4Bytes + kTagBytes;

/** The requester's own tag opens the init body. */
constexpr std::size_t kInitTagOffset = kHeaderSize;
/** The security id closes the init. */
constexpr std::size_t kInitSecurityIdOffset = kInitTagOffset + kTagBytes;

/** The timestamp opens the init ack body. */
constexpr std::size_t kInitAckTimestampOffset = kHeaderSize;
/** The cookie follows the timestamp. */
constexpr std::size_t kInitAckCookieOffset = kInitAckTimestampOffset + sizeof(std::uint32_t);
/** The tag block follows the cookie. */
constexpr std::size_t kInitAckTagsOffset = kInitAckCookieOffset + kCookieSize;
/** The requester address follows the tags. */
constexpr std::size_t kInitAckAddressOffset = kInitAckTagsOffset + kInitAckTagCount * kTagBytes;
/** The security id closes the init ack. */
constexpr std::size_t kInitAckSecurityIdOffset = kInitAckAddressOffset + kAddressBytes;

/** The echoed init ack follows the cookie echo header. */
constexpr std::size_t kCookieEchoInitAckOffset = kHeaderSize;
/** The security id follows the address block and precedes the key. */
constexpr std::size_t kCookieEchoSecurityIdOffset =
    kCookieEchoInitAckOffset + kInitAckSize + kCookieEchoAddressBlockSize;
/** The public key closes the cookie echo. */
constexpr std::size_t kCookieEchoKeyOffset = kCookieEchoSize - kPublicKeySize;

/** The public key follows the cookie ack header. */
constexpr std::size_t kCookieAckKeyOffset = kHeaderSize;
/** The security id closes the cookie ack. */
constexpr std::size_t kCookieAckSecurityIdOffset = kCookieAckKeyOffset + kPublicKeySize;

/**
 * Writes one unsigned value low byte first. Every field of this layer travels that way.
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
        output[offset + index] = static_cast<std::byte>((value >> (index * kByteBits)) & kByteMask);
    }
}

/**
 * Reads one unsigned 16-bit value, low byte first.
 * @param datagram Received bytes.
 * @param offset First byte of the field.
 * @return Host-order value.
 */
[[nodiscard]] std::uint16_t read_word(std::span<const std::byte> datagram,
                                      std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(datagram[offset])
        | (std::to_integer<std::uint16_t>(datagram[offset + 1]) << kByteBits));
}

/**
 * Writes the shared header.
 * @param output Datagram storage.
 * @param type Message type.
 * @param peerTag Tag the receiver checks against its own.
 */
void write_header(std::span<std::byte> output, Type type, std::uint16_t peerTag) noexcept {
    output[kTypeOffset] = static_cast<std::byte>(type);
    output[kVersionOffset] = kVersion;
    write_memory_order(output, kPeerTagOffset, peerTag, sizeof(std::uint16_t));
    write_memory_order(output, kReservedOffset, 0, sizeof(std::uint32_t));
}

/**
 * Writes one address as four address bytes then a port.
 * @param output Datagram storage.
 * @param offset First byte of the field.
 * @param address IPv4 in host order.
 * @param port UDP port in host order.
 */
void write_address(std::span<std::byte> output,
                   std::size_t offset,
                   std::uint32_t address,
                   std::uint16_t port) noexcept {
    // The address travels in dotted order and the port beside it travels low byte first.
    for (std::size_t index = 0; index < kIpv4Bytes; ++index) {
        const unsigned shift = static_cast<unsigned>(kIpv4Bytes - 1 - index) * kByteBits;
        output[offset + index] = static_cast<std::byte>((address >> shift) & kByteMask);
    }
    write_memory_order(output, offset + kIpv4Bytes, port, kTagBytes);
}

} // namespace

/** Reads the message type of one datagram. */
bool read_type(std::span<const std::byte> datagram, std::uint8_t& output) noexcept {
    if (datagram.size() < kHeaderSize) {
        return false;
    }
    output = std::to_integer<std::uint8_t>(datagram[kTypeOffset]);
    return true;
}

/** Reads the verification tag the sender addressed this packet to. */
bool read_peer_tag(std::span<const std::byte> datagram, std::uint16_t& output) noexcept {
    if (datagram.size() < kHeaderSize) {
        return false;
    }
    output = read_word(datagram, kPeerTagOffset);
    return true;
}

/** Reads one init. */
bool read_init(std::span<const std::byte> datagram, Init& output) noexcept {
    std::uint8_t type = 0;
    if (datagram.size() != kInitSize || !read_type(datagram, type)
        || type != static_cast<std::uint8_t>(Type::init)) {
        return false;
    }
    output.initTag = read_word(datagram, kInitTagOffset);
    std::copy_n(
        datagram.begin() + kInitSecurityIdOffset, kSecurityIdSize, output.securityId.begin());
    return true;
}

/** Reads the fields a cookie echo must surrender. */
bool read_cookie_echo(std::span<const std::byte> datagram, CookieEcho& output) noexcept {
    std::uint8_t type = 0;
    if (datagram.size() != kCookieEchoSize || !read_type(datagram, type)
        || type != static_cast<std::uint8_t>(Type::cookieEcho)) {
        return false;
    }
    std::copy_n(
        datagram.begin() + kCookieEchoInitAckOffset, kInitAckSize, output.echoedInitAck.begin());
    std::copy_n(
        datagram.begin() + kCookieEchoSecurityIdOffset, kSecurityIdSize, output.securityId.begin());
    std::copy_n(datagram.begin() + kCookieEchoKeyOffset, kPublicKeySize, output.publicKey.begin());
    return true;
}

/** Builds one init ack. */
void write_init_ack(const InitAck& initAck, std::array<std::byte, kInitAckSize>& output) noexcept {
    output = {};
    write_header(output, Type::initAck, initAck.requesterTag);
    write_memory_order(output, kInitAckTimestampOffset, initAck.timestamp, sizeof(std::uint32_t));
    std::copy(initAck.cookie.begin(), initAck.cookie.end(), output.begin() + kInitAckCookieOffset);
    // The requester adopts the first tag as its peer tag and checks the third against its own.
    write_memory_order(output, kInitAckTagsOffset, initAck.responderTag, kTagBytes);
    write_memory_order(output, kInitAckTagsOffset + kTagBytes, initAck.responderTag, kTagBytes);
    write_memory_order(output, kInitAckTagsOffset + 2 * kTagBytes, initAck.requesterTag, kTagBytes);
    // The last two tags carry a migrating association's old pair. A fresh one leaves them zero.
    write_address(output, kInitAckAddressOffset, initAck.address, initAck.port);
    std::copy(initAck.securityId.begin(),
              initAck.securityId.end(),
              output.begin() + kInitAckSecurityIdOffset);
}

/** Builds one cookie ack. */
void write_cookie_ack(const CookieAck& cookieAck,
                      std::array<std::byte, kCookieAckSize>& output) noexcept {
    output = {};
    write_header(output, Type::cookieAck, cookieAck.requesterTag);
    std::copy(cookieAck.publicKey.begin(),
              cookieAck.publicKey.end(),
              output.begin() + kCookieAckKeyOffset);
    std::copy(cookieAck.securityId.begin(),
              cookieAck.securityId.end(),
              output.begin() + kCookieAckSecurityIdOffset);
}

} // namespace sunrise::middleware::gameplay::dtls
