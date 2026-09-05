#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::core::settings::server::gameplay {

/** Process that owns the gameplay UDP endpoint for this run. */
enum class Topology : std::uint8_t {
    /** No endpoint is bound and no method-0 descriptor is advertised. */
    disabled,
    /** Sunrise binds the endpoint and hosts the peer protocol itself. */
    embedded,
    /** A configured process owns the endpoint. Sunrise binds nothing. */
    external,
};

/** Octets in one IPv4 address. */
inline constexpr std::size_t kAddressOctets = 4;
/** The join descriptor carries an even UDP port, so an odd port is refused at load. */
inline constexpr std::uint16_t kPortAlignment = 2;
/**
 * Host ports the embedded endpoint binds, stepping by the alignment from the configured
 * port. The client keys each channel on the host port it dialled, one port per host row.
 */
inline constexpr std::size_t kHostPortCount = 16;
/** Even, and clear of the discovery ports with the whole pool span. */
inline constexpr std::uint16_t kDefaultPort = 30976;
/** Discovery owns 3074 and 3075. Bound ports are even, so only 3074 can collide. */
inline constexpr std::uint16_t kDiscoveryPort = 3074;
/** One carrier, the live wave, and removal headroom fit in this many reserved slots. */
inline constexpr std::uint16_t kDefaultServerReserve = 256;
/** A smaller reserve cannot hold one carrier plus its removal and quarantine headroom. */
inline constexpr std::uint16_t kMinimumServerReserve = 8;
/** The client keeps at least this many lease bits after the reserve is subtracted. */
inline constexpr std::uint16_t kClientLeaseMinimum = 4096;
/**
 * Entity slots the join hands the client before it asks for any.
 * The whole slot space, capped at what the reserve leaves. That is what the client does.
 */
inline constexpr std::uint16_t kDefaultClientJoinGrant = 8'192;
/** Below this a join cannot cover the client's own low water mark of 400. */
inline constexpr std::uint16_t kMinimumClientJoinGrant = 400;
/**
 * Lease the client is topped up to whenever it asks for more slots. Zero disables the top-up.
 * Disabled by default because a run on 2026-08-25 measured it as useless: the top-up landed
 * (`held=2048`) and the client returned the surplus 32 ms later (`kind=release picked=1840`,
 * back to `held=208`), then failed to create the same three `sobject` entities it always fails
 * on. The client manages its own lease tightly and will not hold slots it has not asked for.
 * Capacity was never the constraint either — it failed with 208 slots held while needing 3
 * entities, and the bubble-14 switch succeeded holding only 151. Kept as a knob because it is
 * the cheapest way to re-run that experiment, not because a value above zero is expected to help.
 */
inline constexpr std::uint16_t kDefaultClientLeaseHighWater = 0;
/** Below the client's own 400 low water mark a top-up would not change what it can create. */
inline constexpr std::uint16_t kMinimumClientLeaseHighWater = 400;

/**
 * Gameplay endpoint topology and the entity-slot split it implies.
 * The advertised address is the one message 12 publishes. The transport address is where the
 * datagram lands after the Client's egress rewrite. Both carry the same port.
 */
struct Settings {
    /** Embedded by default. Public activities reach each other over the peer protocol. */
    Topology topology{Topology::embedded};
    /** Local interface the embedded endpoint binds. */
    std::array<unsigned char, kAddressOctets> bindAddress{127, 0, 0, 1};
    /** Address written into the published descriptor. */
    std::array<unsigned char, kAddressOctets> advertisedAddress{127, 0, 0, 1};
    /** Address the Client's egress rewrite produces. Must reach the bound endpoint. */
    std::array<unsigned char, kAddressOctets> transportAddress{127, 0, 0, 1};
    /** Even UDP port, shared by all three addresses because the egress hook keeps the port. */
    std::uint16_t port{kDefaultPort};
    /** Entity indices held back from the client lease for server-authored entities. */
    std::uint16_t serverReserveCount{kDefaultServerReserve};
    /** Entity indices the join grants. The rest stay free for the client to request. */
    std::uint16_t clientJoinGrantCount{kDefaultClientJoinGrant};
    /**
     * Holds the launch's queuez sync open through the load so the spaceflight legs skip.
     * A family-0 update parks the account player-record mid-transaction, which the cinematic gate
     * refuses; a family-4 completion at arrival frees it. Off by default.
     */
    bool holdLaunchCinematic{false};
    /** Lease one grant tops the client up to, so the next slice set is covered before it asks. */
    std::uint16_t clientLeaseHighWater{kDefaultClientLeaseHighWater};
    /**
     * Ignore the slot mask the client sends on message 21 instead of shrinking its lease by it.
     * Measured 2026-08-25: the client "releases" 7785 of the 7936 slots its join was granted, and
     * 1840 of every later top-up, always within 32 ms and always leaving exactly what it had asked
     * for. A client handing back 98% of a lease it never used is not plausible; a mask that names
     * the slots it is KEEPING, read as the ones it is giving up, produces precisely this. The
     * consequence is real: the client reconciles its own entity bitmap to the host's mask, so the
     * shrunken lease starves entity creation and an encounter bubble kicks to orbit.
     * On, the release is still framed and reported, only the lease is left alone.
     */
    bool ignoreClientSlotRelease{false};
};

/**
 * Checks one gameplay block for internal consistency.
 * @param settings Parsed or default gameplay settings.
 * @return True when the topology, port, and reserve can be bound and advertised together.
 */
[[nodiscard]] bool valid(const Settings& settings) noexcept;

/**
 * Reports the slots actually held back from the client lease.
 * A disabled channel authors no entities, so it reserves nothing and the client keeps the
 * whole slot space.
 * @param settings Active gameplay settings.
 * @return Configured reserve, or zero while the channel is off.
 */
[[nodiscard]] std::uint16_t effective_reserve(const Settings& settings) noexcept;

/**
 * Reports the entity slots one join grants.
 * A disabled channel reserves nothing, so the grant is bounded by the whole slot space instead.
 * @param settings Active gameplay settings.
 * @return Configured grant, capped at what the reserve leaves free.
 */
[[nodiscard]] std::size_t join_grant(const Settings& settings) noexcept;

/**
 * Reports the lease one grant tops the client up to.
 * A disabled channel reserves nothing, so the high water is bounded by the whole slot space.
 * @param settings Active gameplay settings.
 * @return Configured high water, capped at what the reserve leaves free.
 */
[[nodiscard]] std::size_t lease_high_water(const Settings& settings) noexcept;

} // namespace sunrise::core::settings::server::gameplay
