#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::gameplay::dtls {

/** Every association packet opens with the same 8-byte header. */
inline constexpr std::size_t kHeaderSize = 8;
/** The security id every packet carries. The peer routes on it and drops a packet that misses. */
inline constexpr std::size_t kSecurityIdSize = 8;
/** An init is header plus a tag and the security id. */
inline constexpr std::size_t kInitSize = 18;
/** An init ack is header, timestamp, cookie, five tags, an address, and the security id. */
inline constexpr std::size_t kInitAckSize = 52;
/** The cookie the responder issues and the requester echoes. */
inline constexpr std::size_t kCookieSize = 16;
/** An exported public key is padded to this length whatever the curve produced. */
inline constexpr std::size_t kPublicKeySize = 100;
/** The address block a cookie echo carries between the echoed init ack and the security id. */
inline constexpr std::size_t kCookieEchoAddressBlockSize = 41;
/** A cookie echo is header, the whole init ack, an address block, the security id, and the key. */
inline constexpr std::size_t kCookieEchoSize =
    kHeaderSize + kInitAckSize + kCookieEchoAddressBlockSize + kSecurityIdSize + kPublicKeySize;
/** A cookie ack is header, the public key, and the security id. */
inline constexpr std::size_t kCookieAckSize = kHeaderSize + kPublicKeySize + kSecurityIdSize;

/** The 8-byte value the peer keys its association table on. It is opaque and travels verbatim. */
using SecurityId = std::array<std::byte, kSecurityIdSize>;

/** Message types this association understands. Others reach the next router unchanged. */
enum class Type : std::uint8_t {
    init = 1,
    initAck = 2,
    cookieEcho = 3,
    cookieAck = 4,
};

/** Everything one received init carries. */
struct Init {
    /** Verification tag the requester chose for itself. */
    std::uint16_t initTag{};
    /** Security id to echo. Every later packet we send must repeat it or the peer discards it. */
    SecurityId securityId{};
};

/** Everything one received cookie echo carries that the responder reads. */
struct CookieEcho {
    /** The responder's own init ack, returned unaltered. Comparing it is the cookie check. */
    std::array<std::byte, kInitAckSize> echoedInitAck{};
    /** Requester's exported public key. It closes the key exchange. */
    std::array<std::byte, kPublicKeySize> publicKey{};
    /** Security id the requester repeated. */
    SecurityId securityId{};
};

/** Everything one init ack publishes. */
struct InitAck {
    /** Tag the requester chose. It refuses an init ack whose header names a different tag. */
    std::uint16_t requesterTag{};
    /** Tag the responder chose. The requester adopts it as its peer tag. */
    std::uint16_t responderTag{};
    /** Opaque cookie the requester must echo. */
    std::array<std::byte, kCookieSize> cookie{};
    /** Requester's address, in host order. */
    std::uint32_t address{};
    /** Requester's UDP port, in host order. */
    std::uint16_t port{};
    /** Free-running value the requester stores and never checks. */
    std::uint32_t timestamp{};
    /** Security id taken from the init. */
    SecurityId securityId{};
};

/** Everything one cookie ack publishes. */
struct CookieAck {
    /** Tag the requester chose. It refuses a cookie ack whose header names a different tag. */
    std::uint16_t requesterTag{};
    /** Responder's exported public key. */
    std::array<std::byte, kPublicKeySize> publicKey{};
    /** Security id taken from the init. */
    SecurityId securityId{};
};

/**
 * Reads the message type of one datagram.
 * @param datagram Received bytes.
 * @param output Receives the type byte only on success.
 * @return True when the datagram is long enough to carry a header.
 */
[[nodiscard]] bool read_type(std::span<const std::byte> datagram, std::uint8_t& output) noexcept;

/**
 * Reads the verification tag the sender addressed this packet to.
 * @param datagram Received bytes.
 * @param output Receives the tag only on success.
 * @return True when the datagram is long enough to carry a header.
 */
[[nodiscard]] bool read_peer_tag(std::span<const std::byte> datagram,
                                 std::uint16_t& output) noexcept;

/**
 * Reads one init.
 * @param datagram Received bytes.
 * @param output Receives the init only on success.
 * @return True when the datagram is an init of the exact length.
 */
[[nodiscard]] bool read_init(std::span<const std::byte> datagram, Init& output) noexcept;

/**
 * Reads the two fields a cookie echo must surrender.
 * @param datagram Received bytes.
 * @param output Receives the echoed init ack and the public key only on success.
 * @return True when the datagram is a cookie echo of the exact length.
 */
[[nodiscard]] bool read_cookie_echo(std::span<const std::byte> datagram,
                                    CookieEcho& output) noexcept;

/**
 * Builds one init ack.
 * @param initAck Fields to publish.
 * @param output Receives the whole init ack.
 */
void write_init_ack(const InitAck& initAck, std::array<std::byte, kInitAckSize>& output) noexcept;

/**
 * Builds one cookie ack.
 * @param cookieAck Fields to publish.
 * @param output Receives the whole cookie ack.
 */
void write_cookie_ack(const CookieAck& cookieAck,
                      std::array<std::byte, kCookieAckSize>& output) noexcept;

} // namespace sunrise::middleware::gameplay::dtls
