#pragma once
#include "definition.h"

namespace sunrise::state::activity::membership {
inline std::uint8_t next_teleport_token(std::uint8_t previous) noexcept {
    const auto next = static_cast<std::uint8_t>(previous + 1U);
    return next ? next : 1;
}
inline bool matching_teleport(const TeleportState& local, const TeleportState& host) noexcept {
    return local.token == host.token && local.sliceSetIndex == host.sliceSetIndex
        && local.sliceSetHash == host.sliceSetHash;
}
inline void observe_qualified_teleport(MembershipState& member) noexcept {
    if (!member.hasHostTeleport || !matching_teleport(member.teleport, member.hostTeleport)) return;
    if (member.teleport.state == 3 && member.currentReported
        && member.currentRegion.index == member.hostTeleport.sliceSetIndex)
        member.hostTeleport.state = kHostTeleportSpawnState;
    else if (member.hostTeleport.state == kHostTeleportSpawnState && member.teleport.state == 0) {
        member.hasHostTeleport = false;
        member.hostTeleportQualified = false;
        member.hostTeleport = {};
    }
}
}
