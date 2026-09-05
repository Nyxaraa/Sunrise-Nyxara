#pragma once

#include <cstdint>
#include <limits>

namespace sunrise::state::activity::membership {

/** Zero means no published or acknowledged membership revision. */
inline constexpr std::uint32_t kAbsentRevision = 0;
/** The first full identity snapshot starts at revision 1. */
inline constexpr std::uint32_t kInitialRevision = 1;
/** Membership revisions stop before wrap, so a stale acknowledgement cannot look current. */
inline constexpr std::uint32_t kMaximumMembershipRevision =
    (std::numeric_limits<std::uint32_t>::max)();
/** A steady zero epoch keeps the client peer table across unchanged refreshes. */
inline constexpr std::uint32_t kStableEpoch = 0;
/** Derives the membership epoch of one activity record generation. */
[[nodiscard]] inline std::uint32_t session_epoch(std::uint64_t createdRevision) noexcept {
    return static_cast<std::uint32_t>(createdRevision);
}
/** The first local transition uses token 1. */
inline constexpr std::uint8_t kInitialTransitionToken = 1;
/** The mirrored 3-bit state field decodes down to -1. */
inline constexpr std::int8_t kMinimumMirroredState = -1;
/** The mirrored 3-bit state field reaches logical 6. */
inline constexpr std::int8_t kMaximumMirroredState = 6;
/** -1 means no teleport slice set is chosen. */
inline constexpr std::int32_t kAbsentSliceSetIndex = -1;
/**
 * Teleport state that starts the client's arm sequence.
 * `HostTeleport_ReplicatedStateStep` @ `0x7FF742932CE0` runs a four-step machine. Step 0 latches
 * the host token, region and hash while the host state is neither 0 nor 3; step 1 arms the move.
 */
inline constexpr std::int8_t kHostTeleportArmedState = 1;
/**
 * Teleport state that runs the client's spawn.
 * Step 3 of the same machine calls the spawn at `0x7FF742932DA5`, but only while the host state
 * reads 3. Step 0 refuses to re-latch on 3, so holding it there is inert once the spawn has run.
 */
inline constexpr std::int8_t kHostTeleportSpawnState = 3;
/** The mirrored 10-bit slice-set field reaches logical 1022. */
inline constexpr std::int32_t kMaximumSliceSetIndex = 1022;
/** Bubble -1 means no refresh bubble is chosen. */
inline constexpr std::int32_t kMinimumRefreshBubble = -1;
/** Membership refreshes reach at most the 64 bubble slots. */
inline constexpr std::int32_t kMaximumRefreshBubble = 63;
/** All-one bits are not a usable secondary member SOID. */
inline constexpr std::uint64_t kInvalidOpaqueSoid = (std::numeric_limits<std::uint64_t>::max)();
/** All-one bits are not a usable per-join lookup identity. */
inline constexpr std::uint64_t kInvalidJoinIdentity = (std::numeric_limits<std::uint64_t>::max)();

/** Client identity, kept without its source payload bytes. */
struct Identity final {
    std::uint64_t memberKey{};
    std::int32_t smallOpaque{};
    std::int32_t signedOpaque{};
    std::uint64_t joinIdentity{};
    std::uint64_t accountSoid{};
    /** Its role is not verified. Only the nonzero SOID rule is known. */
    std::uint64_t opaqueSoid{};
    /** The role of this second type-23 scalar is not verified. */
    std::uint64_t secondaryOpaque{};
};

/** Spawn state kept for the current activity host, in no wire form. */
struct SpawnState final {
    std::int8_t state{};
    std::uint8_t opaqueByte{};
    std::uint64_t opaqueValue{};
};

/** Teleport state kept for the current activity host, in no wire form. */
struct TeleportState final {
    std::int8_t state{};
    std::uint8_t token{};
    std::int32_t sliceSetIndex{kAbsentSliceSetIndex};
    std::uint32_t sliceSetHash{};
};

/** -1 means the client reported no region. */
inline constexpr std::int32_t kAbsentRegionIndex = -1;
/** D6's biased 32-bit region field reaches the signed positive maximum. */
inline constexpr std::int32_t kMaximumRegionIndex = (std::numeric_limits<std::int32_t>::max)();

/**
 * One region leg of the client's member record, kept exactly as reported so message 12 can
 * mirror it back. It holds the region index, slice-set name hash, index and two 2-bit fields.
 */
struct RegionState final {
    std::int32_t index{kAbsentRegionIndex};
    std::uint32_t hash{};
    std::int32_t sliceSetIndex{kAbsentSliceSetIndex};
    std::int8_t publicState{-1};
    std::int8_t auxState{-1};
};

/**
 * Sparse client-reported state accepted only through one host-owned State transaction.
 * `currentRegion` is the region of the slice set the client holds, -1 while it holds none.
 * `region` is the pending leg: the region it is loading, or the one behind a z-leg switch.
 */
struct AuthoritativeUpdate final {
    SpawnState spawn{};
    TeleportState teleport{};
    RegionState currentRegion{};
    RegionState region{};
    std::uint8_t transitionToken{};
    bool hasTransitionToken{};
    bool hasSpawn{};
    bool hasTeleport{};
    bool hasCurrentRegion{};
    bool hasRegion{};
};

/** Data read under one lock, enough to encode a full membership refresh. */
struct Snapshot final {
    Identity identity{};
    SpawnState spawn{};
    TeleportState teleport{};
    /** The client's own region legs, mirrored into its member record once reported. */
    RegionState currentLeg{};
    RegionState pendingLeg{};
    std::uint32_t revision{};
    std::uint32_t epoch{kStableEpoch};
    std::uint8_t transitionToken{};
    bool hasCurrentLeg{};
    bool hasPendingLeg{};
};

/** Mutable membership fields owned by one activity session. */
struct MembershipState final {
    Identity identity{};
    SpawnState spawn{};
    TeleportState teleport{};
    /**
     * Region the host is moving the client to, replacing the mirror while it is set.
     * `SliceSetTransitionMgr_ArmHostTeleport` reads these four fields and starts a type-7
     * transition. Reporting the region raises the state to the spawn value; nothing clears it.
     */
    TeleportState hostTeleport{};
    bool hasHostTeleport{};
    /** Region of the slice set the client holds; -1 while it holds none. */
    RegionState currentRegion{};
    /** Pending region leg as last reported; -1 once a transition has completed. */
    RegionState region{};
    /** Set once the client has reported each leg, so message 12 mirrors only reported legs. */
    bool currentReported{};
    bool pendingReported{};
    /** Bubble the client's last message-18 refresh named as current; -1 before one arrives. */
    std::int32_t bubble{kMinimumRefreshBubble};
    /** Membership revision that refresh said the client had applied. */
    std::uint32_t bubbleRevision{kAbsentRevision};
    /** The character write-back (ws 702) reports the in-world state while a region is held. */
    bool entered{};
    std::uint32_t revision{};
    /** Stable within one session; changes on world replacement to clear the reused client table. */
    std::uint32_t epoch{kStableEpoch};
    std::uint32_t acknowledgedRevision{};
    std::uint8_t transitionToken{};
    /** Tells an explicit zero token apart from the initial fallback token. */
    bool hasTransitionToken{};
    bool hasIdentity{};
};

/** Safe numeric after-image produced by one committed message-22 State transaction. */
struct CommittedClientState final {
    /** The pending leg: the region the client is loading or precaching. */
    RegionState region{};
    /** The current leg: the region of the slice set the client holds. */
    RegionState currentRegion{};
    std::uint64_t activityStateRevision{};
    std::uint32_t membershipRevision{};
    std::uint32_t teleportSliceSetHash{};
    std::int32_t teleportSliceSetIndex{kAbsentSliceSetIndex};
    /**
     * Region the client holds after this report, whether or not the report moved it.
     * The has-value flags answer "what changed"; this answers "where is the client", which a
     * report that moved some other field does not restate. -1 while the client holds none.
     */
    std::int32_t heldRegion{kAbsentRegionIndex};
    std::int8_t spawnState{};
    std::int8_t teleportState{};
    bool hasRegion{};
    /** The current leg moved in this report and names a held region. */
    bool hasCurrentRegion{};
    bool hasSpawn{};
    bool hasTeleport{};
    bool changed{};
    bool committed{};
};

/** What one prepared membership transaction does. */
enum class MutationKind : std::uint8_t {
    none,
    identity,
    authoritative,
    refresh,
    republish,
    acknowledgement,
};

} // namespace sunrise::state::activity::membership
