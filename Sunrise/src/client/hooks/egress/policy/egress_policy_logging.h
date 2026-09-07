#pragma once

#include "policy.h"

namespace sunrise::client::hooks::egress::policy {

/**
 * Logs one fixed debug event, with no endpoint or payload data.
 * @param operation Socket operation category.
 * @param targetsRedirect True when the checked target is the exact redirect target.
 * @param allowed True when the original can be called.
 */
void log_decision(SocketOperation operation, bool targetsRedirect, bool allowed) noexcept;

/**
 * Logs the destination one datagram send or connect actually targets, capped per process.
 * Diagnostic only: it names where the client dials so a channel that never reaches the host can
 * be told from one that is answered wrong.
 * @param operation Socket operation category.
 * @param destination Original caller destination, before the redirect rewrite. May be null.
 * @param destinationLength Available destination bytes.
 * @param payloadBytes Datagram length, or zero for a connect.
 */
void log_send_target(SocketOperation operation,
                     const sockaddr* destination,
                     int destinationLength,
                     std::size_t payloadBytes) noexcept;

/**
 * Logs the connected peer one send targets, capped per process. Diagnostic only.
 * A connected socket carries no destination in the call, so its peer is read from the socket.
 * @param socket Connected socket the send uses.
 * @param payloadBytes Send length.
 */
void log_send_peer(SOCKET socket, std::size_t payloadBytes) noexcept;

} // namespace sunrise::client::hooks::egress::policy
