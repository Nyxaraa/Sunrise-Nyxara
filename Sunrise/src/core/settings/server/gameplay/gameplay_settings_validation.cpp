#include <cstdint>
#include <limits>

#include "../../../../state/activity/entity_slots/definition.h"
#include "definition.h"

namespace sunrise::core::settings::server::gameplay {

namespace {

/** @return True when the slot split leaves both sides a usable share. */
[[nodiscard]] bool reserve_fits(std::uint16_t reserve) noexcept {
    // One activity session owns exactly this many entity-slot lease bits.
    constexpr std::size_t kSlotCount = state::activity::entity_slots::kSlotCount;
    if (reserve < kMinimumServerReserve || static_cast<std::size_t>(reserve) >= kSlotCount) {
        return false;
    }
    return kSlotCount - static_cast<std::size_t>(reserve) >= kClientLeaseMinimum;
}

} // namespace

/** Checks one gameplay block for internal consistency. */
bool valid(const Settings& settings) noexcept {
    if (settings.topology == Topology::disabled) {
        return true;
    }
    if (settings.port == 0 || settings.port % kPortAlignment != 0) {
        return false;
    }
    // The endpoint binds kHostPortCount ports stepping by kPortAlignment, so the whole span
    // must stay below 65536 and clear of the discovery port, not just the configured port.
    constexpr std::uint32_t kPoolSpan =
        static_cast<std::uint32_t>(kHostPortCount - 1) * kPortAlignment;
    const std::uint32_t lastPort = static_cast<std::uint32_t>(settings.port) + kPoolSpan;
    if (lastPort > (std::numeric_limits<std::uint16_t>::max)()) {
        return false;
    }
    if (settings.port <= kDiscoveryPort && kDiscoveryPort <= lastPort) {
        return false;
    }
    if (!reserve_fits(settings.serverReserveCount)) {
        return false;
    }
    // The grant only has to name a real slot count. The join caps it at whatever the reserve
    // leaves. Requiring it to fit beside the reserve here would refuse the default, which asks
    // for the whole space on purpose.
    constexpr std::size_t kSlotCount = state::activity::entity_slots::kSlotCount;
    // Zero disables the top-up. Any other value must still clear the client's own low water.
    if (settings.clientLeaseHighWater != 0
        && (settings.clientLeaseHighWater < kMinimumClientLeaseHighWater
            || static_cast<std::size_t>(settings.clientLeaseHighWater) > kSlotCount)) {
        return false;
    }
    return settings.clientJoinGrantCount >= kMinimumClientJoinGrant
           && static_cast<std::size_t>(settings.clientJoinGrantCount) <= kSlotCount;
}

/** Reports the slots actually held back from the client lease. */
std::uint16_t effective_reserve(const Settings& settings) noexcept {
    return settings.topology == Topology::disabled ? 0 : settings.serverReserveCount;
}

/** Reports the entity slots one join grants. */
std::size_t join_grant(const Settings& settings) noexcept {
    // One activity session owns exactly this many entity-slot lease bits.
    constexpr std::size_t kSlotCount = state::activity::entity_slots::kSlotCount;
    const std::size_t free = kSlotCount - static_cast<std::size_t>(effective_reserve(settings));
    const std::size_t wanted = settings.clientJoinGrantCount;
    return wanted < free ? wanted : free;
}

/** Reports the lease one grant tops the client up to. */
std::size_t lease_high_water(const Settings& settings) noexcept {
    // One activity session owns exactly this many entity-slot lease bits.
    constexpr std::size_t kSlotCount = state::activity::entity_slots::kSlotCount;
    const std::size_t free = kSlotCount - static_cast<std::size_t>(effective_reserve(settings));
    const std::size_t wanted = settings.clientLeaseHighWater;
    return wanted < free ? wanted : free;
}

} // namespace sunrise::core::settings::server::gameplay
