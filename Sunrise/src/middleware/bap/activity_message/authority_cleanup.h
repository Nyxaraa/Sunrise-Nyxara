#pragma once

#include "activity_host_control.h"
#include "entity_authority.h"

namespace sunrise::middleware::bap::activity_message::authority_cleanup {

// Only the requesting client's explicit abandoned mask is eligible. Never infer
// an all-slots reset from elapsed time, pressure, or a region's numeric range.
[[nodiscard]] inline bool prepare(std::uint32_t messageType,
                                  std::span<const std::byte> payload,
                                  std::uint8_t currentEpoch,
                                  host_control::PurgeAuthorityBody& output) noexcept {
    host_control::PurgeAuthorityBody candidate{};
    // Native 170B030 compares the promoted byte with current + 1; zero
    // cannot wrap past 255. Never send a repeated epoch or guess a reset.
    if (currentEpoch == 255) return false;
    candidate.epoch = static_cast<std::uint8_t>(currentEpoch + 1U);
    if (messageType == entity_authority::kRequestPurgeMessageType) {
        entity_authority::PurgeRequest request{};
        if (!entity_authority::parse_request_purge(payload, request)) return false;
        candidate.slots = request.mask;
        candidate.reason = static_cast<std::int8_t>(request.reason);
    } else if (messageType == entity_authority::kAbandonMessageType) {
        entity_authority::Release release{};
        if (!entity_authority::parse_abandon(payload, release)) return false;
        candidate.slots = release.mask;
        candidate.reason = static_cast<std::int8_t>(release.reason);
    } else {
        // Msg33 can relinquish authority over live objects inside the occupied
        // bubble. It is not a request to delete those objects.
        return false;
    }
    bool any = false;
    for (const auto byte : candidate.slots) any |= byte != std::byte{};
    if (!any) return false;
    output = candidate;
    return true;
}
}
