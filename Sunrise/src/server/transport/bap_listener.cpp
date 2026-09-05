#include "bap_listener.h"

#include <WS2tcpip.h>
#include <WinSock2.h>
#include <array>
#include <cstdio>

#include "../../core/logging/log.h"
#include "../../core/settings/settings.h"
#include "../activity/host_runtime.h"
#include "core/threading/data_mutex.h"
#include "internal.h"

namespace sunrise::server::transport {

namespace {

core::threading::DataMutex<Listener> g_listener;

/** Makes one socket nonblocking. @return True when it can no longer block its caller. */
[[nodiscard]] bool make_nonblocking(SOCKET socket) noexcept {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) != SOCKET_ERROR;
}

/** @return Index of the first unused peer slot, or the slot count when all are taken. */
[[nodiscard]] std::size_t free_slot(const Listener& listener) noexcept {
    for (std::size_t slot = 0; slot < listener.peers.size(); ++slot) {
        if (listener.peers[slot].socket == INVALID_SOCKET) {
            return slot;
        }
    }
    return listener.peers.size();
}

/**
 * Takes one waiting connection into a free slot and opens its Server session.
 * @param slot Peer slot already checked to be free.
 */
void accept_peer(Listener& listener, std::size_t slot) noexcept {
    const SOCKET accepted = accept(listener.acceptor, nullptr, nullptr);
    if (accepted == INVALID_SOCKET) {
        return;
    }
    if (!make_nonblocking(accepted)) {
        closesocket(accepted);
        return;
    }
    Peer& peer = listener.peers[slot];
    peer.socket = accepted;
    peer.streamSize = 0;
    peer.outputOffset = 0;
    peer.outputSize = 0;
    peer.connectionId = connection_id(slot);

    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=transport stage=accept result=ok conn=%u", peer.connectionId);
    if (written > 0) {
        const std::size_t length = static_cast<std::size_t>(written) < line.size()
                                       ? static_cast<std::size_t>(written)
                                       : line.size() - 1;
        core::log::write(core::log::Channel::server, core::log::Level::info, {line.data(), length});
    }
    if (!offer(peer, client::network::BapEvent::open, {})) {
        close_peer(peer);
    }
}

/**
 * Reads at most once from one readable peer.
 * @param slot Live peer slot reported readable.
 */
void receive_peer(Peer& peer) noexcept {
    const std::size_t free = kStreamCapacity - peer.streamSize;
    if (free == 0) {
        return;
    }
    const int received = recv(peer.socket,
                              reinterpret_cast<char*>(peer.stream.data() + peer.streamSize),
                              static_cast<int>(free),
                              0);
    if (received > 0) {
        peer.streamSize += static_cast<std::size_t>(received);
        return;
    }
    if (received == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
        close_peer(peer);
    }
}

/**
 * Sends at most once from one peer's committed output.
 * @param slot Live peer slot.
 * @return True while the peer remains usable.
 */
[[nodiscard]] bool flush_peer(Peer& peer) noexcept {
    if (peer.outputSize == 0) {
        return true;
    }
    const std::size_t remaining = peer.outputSize - peer.outputOffset;
    const int sent = send(peer.socket,
                          reinterpret_cast<const char*>(peer.output.data() + peer.outputOffset),
                          static_cast<int>(remaining),
                          0);
    if (sent > 0) {
        return advance_output(peer, static_cast<std::size_t>(sent));
    }
    return sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK;
}

/**
 * Services one peer with one read, frame, write and due-poll budget.
 * @param readable Ready-read set from select.
 * @param writable Ready-write set from select.
 * @param wasPending True when select saw output.
 * @param pollDue True on a poll tick.
 */
void service_peer(
    Peer& peer, fd_set& readable, fd_set& writable, bool wasPending, bool pollDue) noexcept {
    bool sent = false;
    if (wasPending && FD_ISSET(peer.socket, &writable)) {
        sent = true;
        if (!flush_peer(peer)) {
            close_peer(peer);
            return;
        }
    }
    if (peer.socket != INVALID_SOCKET && FD_ISSET(peer.socket, &readable)) {
        receive_peer(peer);
    }
    if (peer.socket == INVALID_SOCKET) {
        return;
    }
    if (peer.outputSize == 0 && !drain_stream(peer)) {
        close_peer(peer);
        return;
    }
    if (pollDue && peer.outputSize == 0 && !offer(peer, client::network::BapEvent::poll, {})) {
        close_peer(peer);
        return;
    }
    if (!wasPending && !sent && peer.outputSize != 0 && !flush_peer(peer)) {
        close_peer(peer);
    }
}

} // namespace

/** Starts the nonblocking loopback listener on one port. */
bool initialize_on_port(Listener& listener, std::uint16_t port) noexcept {
    if (listener.active) {
        return true;
    }
    // This DLL initializes before the game touches Winsock, so the listener starts it itself.
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        return false;
    }
    listener.winsockOwned = true;
    listener.acceptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener.acceptor == INVALID_SOCKET) {
        WSACleanup();
        listener.winsockOwned = false;
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    BOOL reuse = TRUE;
    (void)setsockopt(listener.acceptor,
                     SOL_SOCKET,
                     SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuse),
                     sizeof reuse);
    if (!make_nonblocking(listener.acceptor)
        || bind(listener.acceptor, reinterpret_cast<const sockaddr*>(&address), sizeof address)
               == SOCKET_ERROR
        || listen(listener.acceptor, static_cast<int>(listener.peers.size())) == SOCKET_ERROR) {
        closesocket(listener.acceptor);
        listener.acceptor = INVALID_SOCKET;
        WSACleanup();
        listener.winsockOwned = false;
        return false;
    }
    listener.active = true;
    listener.nextPollTick = 0;
    std::array<char, 64> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=transport stage=listen result=ok port=%u",
                                      static_cast<unsigned>(port));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return true;
}

/** Starts the nonblocking listener on the configured BAP port. */
bool initialize() noexcept {
    return g_listener.lock([](Listener& listener) {
        return initialize_on_port(listener, core::settings::get().server.bapPort);
    });
}

/** Runs one bounded listener slice on the caller thread. @param now Monotonic tick count. */
void service(std::uint64_t now) noexcept {
    g_listener.try_lock([now](Listener& listener) {
        if (!listener.active) {
            return;
        }

        fd_set readable;
        fd_set writable;
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        std::array<bool, client::network::kBapConnectionCount> wasPending{};
        const std::size_t accepting = free_slot(listener);
        // With no free slot the acceptor is left out of the set, so a connect waits in the backlog
        // with no handshake and no other symptom. Report the edge.
        const bool full = accepting == listener.peers.size();
        if (full != listener.slotsFull) {
            listener.slotsFull = full;
            std::array<char, core::log::kLineCapacity> line{};
            const int written = std::snprintf(line.data(),
                                              line.size(),
                                              "ev=transport stage=accept result=%s slots=%zu",
                                              full ? "full" : "free",
                                              listener.peers.size());
            if (written > 0) {
                core::log::write(core::log::Channel::server,
                                 full ? core::log::Level::warn : core::log::Level::info,
                                 {line.data(), static_cast<std::size_t>(written)});
            }
        }
        if (!full) {
            FD_SET(listener.acceptor, &readable);
        }
        for (std::size_t slot = 0; slot < listener.peers.size(); ++slot) {
            const Peer& peer = listener.peers[slot];
            if (peer.socket == INVALID_SOCKET) {
                continue;
            }
            if (peer.streamSize < kStreamCapacity) {
                FD_SET(peer.socket, &readable);
            }
            if (peer.outputSize != 0) {
                FD_SET(peer.socket, &writable);
                wasPending[slot] = true;
            }
        }
        timeval timeout{};
        if (select(0, &readable, &writable, nullptr, &timeout) == SOCKET_ERROR) {
            return;
        }

        const bool timedPoll = listener.nextPollTick == 0 || now >= listener.nextPollTick;
        if (timedPoll) {
            listener.nextPollTick = now + static_cast<std::uint64_t>(kServiceIntervalMs);
        }
        // The poll is what lets a committed answer out. Holding one for the rest of the
        // interval costs every queued mission action a full interval of its own.
        const bool pollDue = timedPoll || activity::host::any_output_pending();
        if (!full && FD_ISSET(listener.acceptor, &readable)) {
            accept_peer(listener, accepting);
        }
        for (std::size_t slot = 0; slot < listener.peers.size(); ++slot) {
            Peer& peer = listener.peers[slot];
            if (peer.socket != INVALID_SOCKET) {
                service_peer(peer, readable, writable, wasPending[slot], pollDue);
            }
        }
    });
}

/** Closes every socket owned by the listener. */
void shutdown() noexcept {
    g_listener.lock([](Listener& listener) {
        if (!listener.active) {
            return;
        }
        listener.active = false;
        if (listener.acceptor != INVALID_SOCKET) {
            closesocket(listener.acceptor);
            listener.acceptor = INVALID_SOCKET;
        }
        for (Peer& peer : listener.peers) {
            close_peer(peer);
        }
        listener.nextPollTick = 0;
        listener.slotsFull = false;
        if (listener.winsockOwned) {
            WSACleanup();
            listener.winsockOwned = false;
        }
    });
}

} // namespace sunrise::server::transport
