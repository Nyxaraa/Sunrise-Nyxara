#include "gameplay_endpoint.h"

#include <WS2tcpip.h>
#include <WinSock2.h>
#include <array>
#include <atomic>

#include "../../../core/settings/settings.h"
#include "../../../middleware/crypto/random_bytes.h"
#include "../../../middleware/gameplay/nat/introduction.h"
#include "../association/association_host.h"
#include "../dtls/dtls_host.h"
#include "../gameplay_log.h"

namespace sunrise::server::gameplay::endpoint {

namespace {

namespace settings = core::settings::server::gameplay;

/** Pool size and stride live in settings so validation covers the same span the pool binds. */
using settings::kHostPortCount;
using settings::kPortAlignment;
/** Slot holding the configured port. Everything that names no host port lands here. */
constexpr std::size_t kPrimarySlot = 0;
/** One receive slice drains at most this many datagrams per bound port. */
constexpr unsigned kReceiveBudget = 8;
/** Largest datagram accepted. Anything longer is dropped before it is parsed. */
constexpr std::size_t kDatagramCapacity = 1500;
/** Arrivals reported per run. Enough to show a handshake without a flood filling the log. */
constexpr unsigned kMaxReceiveReports = 64;
/** Traversal replies reported per run. The client repeats a request until the address resolves. */
constexpr unsigned kMaxTraversalReports = 8;

/** Arrivals and traversal replies already reported, counted against the budgets above. */
std::atomic<unsigned> g_reported{0};
std::atomic<unsigned> g_traversalReported{0};

/** @return A pool with every slot unbound. Zero is a usable descriptor, so it cannot mark one. */
[[nodiscard]] consteval std::array<SOCKET, kHostPortCount> unbound_sockets() noexcept {
    std::array<SOCKET, kHostPortCount> sockets{};
    sockets.fill(INVALID_SOCKET);
    return sockets;
}

/** Socket pool and identity, serviced only while the lifecycle lock is held. */
struct EndpointState {
    std::array<SOCKET, kHostPortCount> sockets = unbound_sockets();
    std::array<std::uint16_t, kHostPortCount> ports{};
    bool winsockOwned{};
    bool ready{};
    state::gameplay::Endpoint advertised{};
    Identity identity{};
};

/** @return Pool slot bound to one host port, or the primary slot when the port is not ours. */
[[nodiscard]] std::size_t slot_for_port(const EndpointState& pool, std::uint16_t port) noexcept {
    // An unbound slot carries port zero, so it must not answer for a datagram that names none.
    for (std::size_t slot = 0; port != 0 && slot < kHostPortCount; ++slot) {
        if (pool.ports[slot] == port) {
            return slot;
        }
    }
    return kPrimarySlot;
}

/**
 * Generates one nonzero identity value.
 * @param output Receives a random nonzero value.
 * @return True when Windows produced the bytes.
 */
[[nodiscard]] bool generate_identity(std::uint64_t& output) noexcept {
    // The identity is assembled low byte first, matching its raw descriptor field.
    constexpr unsigned kByteBits = 8;
    std::array<std::byte, sizeof(std::uint64_t)> bytes{};
    if (!middleware::crypto::random::fill(bytes)) {
        return false;
    }
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        value |= std::to_integer<std::uint64_t>(bytes[index]) << (index * kByteBits);
    }
    // Zero reads as absent in both the descriptor and the region record.
    output = value == 0 ? 1 : value;
    return true;
}

SRWLOCK g_lock{SRWLOCK_INIT};
EndpointState g_endpoint;

/**
 * Folds configured octets into one address value.
 * @param octets Dotted-quad order.
 * @return Host-order IPv4 value.
 */
[[nodiscard]] std::uint32_t
host_address(const std::array<unsigned char, settings::kAddressOctets>& octets) noexcept {
    /** One IPv4 octet is 8 bits, so each fold shifts by that much. */
    constexpr unsigned kOctetBits = 8;
    std::uint32_t value = 0;
    for (const unsigned char octet : octets) {
        value = (value << kOctetBits) | octet;
    }
    return value;
}

/** Makes one socket nonblocking. @return True when it can no longer block its caller. */
[[nodiscard]] bool make_nonblocking(SOCKET socket) noexcept {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) != SOCKET_ERROR;
}

/** Closes every socket and drops the Winsock reference. Callers already hold the lock. */
void close_locked() noexcept {
    for (std::size_t slot = 0; slot < kHostPortCount; ++slot) {
        if (g_endpoint.sockets[slot] != INVALID_SOCKET) {
            closesocket(g_endpoint.sockets[slot]);
        }
        g_endpoint.sockets[slot] = INVALID_SOCKET;
        g_endpoint.ports[slot] = 0;
    }
    if (g_endpoint.winsockOwned) {
        WSACleanup();
        g_endpoint.winsockOwned = false;
    }
}

/**
 * Binds one pool socket.
 * @param bindAddress Host-order interface to bind.
 * @param port Host port this slot answers on.
 * @param output Receives the bound socket.
 * @return True when the socket is bound and nonblocking.
 */
[[nodiscard]] bool
bind_one(std::uint32_t bindAddress, std::uint16_t port, SOCKET& output) noexcept {
    output = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (output == INVALID_SOCKET) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(bindAddress);
    if (!make_nonblocking(output)
        || bind(output, reinterpret_cast<const sockaddr*>(&address), sizeof address)
               == SOCKET_ERROR) {
        closesocket(output);
        output = INVALID_SOCKET;
        return false;
    }
    return true;
}

/**
 * Binds the whole embedded port pool.
 * @param configured Validated gameplay settings.
 * @return True when every port is bound and nonblocking.
 */
[[nodiscard]] bool bind_embedded(const settings::Settings& configured) noexcept {
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        return false;
    }
    g_endpoint.winsockOwned = true;
    const std::uint32_t bindAddress = host_address(configured.bindAddress);
    for (std::size_t slot = 0; slot < kHostPortCount; ++slot) {
        // Validation already proved the whole span fits below 65536, so the cast cannot wrap.
        const auto port = static_cast<std::uint16_t>(configured.port + slot * kPortAlignment);
        if (bind_one(bindAddress, port, g_endpoint.sockets[slot])) {
            g_endpoint.ports[slot] = port;
            continue;
        }
        // Only the configured port is required. A pool port another process already holds costs
        // that row its own channel and nothing else, so it must not take the endpoint down.
        if (slot == kPrimarySlot) {
            close_locked();
            return false;
        }
    }
    return true;
}

/** @return Pool ports that bound. Callers already hold the lock. */
[[nodiscard]] std::size_t bound_ports_locked() noexcept {
    std::size_t count = 0;
    for (const std::uint16_t port : g_endpoint.ports) {
        count += port != 0 ? 1U : 0U;
    }
    return count;
}

/**
 * Takes at most one datagram off the socket.
 * The lock is released before the caller routes, because routing sends replies through the
 * same endpoint and this lock is not recursive.
 * @param slot Pool slot to drain.
 * @param buffer Receives the datagram bytes.
 * @param from Receives the source endpoint in host order, plus the local port keying the link.
 * @param size Receives the datagram length.
 * @return True when one datagram was read.
 */
[[nodiscard]] bool receive_once(std::size_t slot,
                                std::span<std::byte> buffer,
                                state::gameplay::Endpoint& from,
                                std::size_t& size) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const SOCKET socket = g_endpoint.sockets[slot];
    const std::uint16_t localPort = g_endpoint.ports[slot];
    if (socket == INVALID_SOCKET) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    sockaddr_in source{};
    int sourceSize = sizeof source;
    const int received = recvfrom(socket,
                                  reinterpret_cast<char*>(buffer.data()),
                                  static_cast<int>(buffer.size()),
                                  0,
                                  reinterpret_cast<sockaddr*>(&source),
                                  &sourceSize);
    ReleaseSRWLockExclusive(&g_lock);
    if (received <= 0) {
        return false;
    }
    from.address = ntohl(source.sin_addr.s_addr);
    from.port = ntohs(source.sin_port);
    // The peer sends every channel from one source port, so the port it dialled is the only thing
    // that tells two links from the same peer apart. Everything downstream keys on it.
    from.localPort = localPort;
    size = static_cast<std::size_t>(received);
    return true;
}

} // namespace

/** Binds the gameplay UDP endpoint when the configured topology is embedded. */
bool initialize() noexcept {
    const settings::Settings& configured = core::settings::get().server.gameplay;
    AcquireSRWLockExclusive(&g_lock);
    if (g_endpoint.ready) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    if (!settings::valid(configured)) {
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::error, "ev=gameplay stage=endpoint result=fail reason=settings");
        return false;
    }
    if (configured.topology == settings::Topology::disabled) {
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::info, "ev=gameplay stage=endpoint result=skip reason=disabled");
        return true;
    }
    // The descriptor advertises one endpoint and the egress rewrite keeps its port, so the
    // transport endpoint must land on the same port the socket binds.
    if (configured.topology == settings::Topology::embedded && !bind_embedded(configured)) {
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::error, "ev=gameplay stage=endpoint result=fail reason=bind");
        return false;
    }
    if (!generate_identity(g_endpoint.identity.machineId)
        || !generate_identity(g_endpoint.identity.onlineSessionId)) {
        close_locked();
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::error, "ev=gameplay stage=endpoint result=fail reason=identity");
        return false;
    }
    const auto& descriptorAddress = configured.topology == settings::Topology::embedded
                                        ? configured.transportAddress
                                        : configured.advertisedAddress;
    g_endpoint.advertised.address = host_address(descriptorAddress);
    g_endpoint.advertised.port = configured.port;
    g_endpoint.advertised.localPort = configured.port;
    g_endpoint.ready = true;
    const bool embedded = configured.topology == settings::Topology::embedded;
    const std::size_t bound = bound_ports_locked();
    ReleaseSRWLockExclusive(&g_lock);
    report(core::log::Level::info,
           "ev=gameplay stage=endpoint result=ok mode=%s port=%u ports=%zu of=%zu",
           embedded ? "embedded" : "external",
           static_cast<unsigned>(configured.port),
           bound,
           kHostPortCount);
    return true;
}

/** Drains a bounded number of datagrams and expires stale associations. */
void service(std::uint64_t now) noexcept {
    for (std::size_t slot = 0; slot < kHostPortCount; ++slot) {
        for (unsigned drained = 0; drained < kReceiveBudget; ++drained) {
            std::array<std::byte, kDatagramCapacity> buffer{};
            state::gameplay::Endpoint from{};
            std::size_t size = 0;
            if (!receive_once(slot, buffer, from, size)) {
                break;
            }
            // Routing reports only what it recognises, so without this an arrival and a silent drop
            // read the same. The budget keeps a flood off the log.
            if (g_reported.fetch_add(1, std::memory_order_relaxed) < kMaxReceiveReports) {
                report(core::log::Level::info,
                       "ev=gameplay stage=receive result=ok from=0x%08X:%u bytes=%zu b0=0x%02X",
                       from.address,
                       static_cast<unsigned>(from.port),
                       size,
                       size != 0 ? static_cast<unsigned>(buffer[0]) : 0U);
            }
            // A traversal request is answered before routing: the association layer has no reader
            // for it, and until it is answered the client never resolves this address.
            if (middleware::gameplay::nat::make_introduction_reply({buffer.data(), size})) {
                const bool sent = send_to(from, {buffer.data(), size});
                if (g_traversalReported.fetch_add(1, std::memory_order_relaxed)
                    < kMaxTraversalReports) {
                    report(core::log::Level::info,
                           "ev=gameplay stage=traversal result=%s from=0x%08X:%u",
                           sent ? "reply" : "send_failed",
                           from.address,
                           static_cast<unsigned>(from.port));
                }
                continue;
            }
            // The peer opens every connection with the Demonware association handshake, so this
            // runs ahead of the engine association the same port also carries.
            if (dtls::route(from, {buffer.data(), size}, now)) {
                continue;
            }
            association::route(from, {buffer.data(), size}, now);
        }
    }
    association::expire(now);
}

/** Sends one datagram to a client endpoint. */
bool send_to(const state::gameplay::Endpoint& destination,
             std::span<const std::byte> datagram) noexcept {
    if (datagram.empty() || destination.port == 0) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(destination.port);
    address.sin_addr.s_addr = htonl(destination.address);
    AcquireSRWLockShared(&g_lock);
    // The reply leaves the port the peer dialled, because that is the channel it is waiting on.
    const SOCKET socket = g_endpoint.sockets[slot_for_port(g_endpoint, destination.localPort)];
    const int sent = socket == INVALID_SOCKET
                         ? SOCKET_ERROR
                         : sendto(socket,
                                  reinterpret_cast<const char*>(datagram.data()),
                                  static_cast<int>(datagram.size()),
                                  0,
                                  reinterpret_cast<const sockaddr*>(&address),
                                  sizeof address);
    ReleaseSRWLockShared(&g_lock);
    return sent == static_cast<int>(datagram.size());
}

/** Closes the socket and releases the Winsock reference this module took. */
void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    close_locked();
    g_endpoint.ready = false;
    g_endpoint.advertised = {};
    g_endpoint.identity = {};
    ReleaseSRWLockExclusive(&g_lock);
}

/** Reports endpoint readiness. */
bool ready() noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool value = g_endpoint.ready;
    ReleaseSRWLockShared(&g_lock);
    return value;
}

/** Reports the endpoint published in the join descriptor. */
state::gameplay::Endpoint advertised() noexcept {
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::Endpoint value = g_endpoint.advertised;
    ReleaseSRWLockShared(&g_lock);
    return value;
}

/** Reports the host port one activity-host row advertises. */
std::uint16_t host_port(std::size_t row) noexcept {
    AcquireSRWLockShared(&g_lock);
    const std::uint16_t value =
        row < kHostPortCount ? g_endpoint.ports[row] : g_endpoint.advertised.port;
    ReleaseSRWLockShared(&g_lock);
    return value;
}

/** Reports the identity generated when the endpoint bound. */
Identity identity() noexcept {
    AcquireSRWLockShared(&g_lock);
    const Identity value = g_endpoint.identity;
    ReleaseSRWLockShared(&g_lock);
    return value;
}

} // namespace sunrise::server::gameplay::endpoint
