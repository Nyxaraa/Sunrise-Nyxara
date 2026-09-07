#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::middleware::bap::activity_message::peer_ledger {

/** The client returns one peer reservation it no longer needs. */
inline constexpr std::uint32_t kReleaseReservationType = 14;
/** The client reports that one peer is leaving. */
inline constexpr std::uint32_t kPeerLeaveType = 15;
/** The client reports that it cannot reach one peer. Both sides can send this type. */
inline constexpr std::uint32_t kConnectivityFailureType = 37;
/** The client proposes a host migration. Nothing here acts on one. */
inline constexpr std::uint32_t kSpeculativeMigrationType = 48;

/** Release carries two scalars and a peer key in exactly 16 bytes. */
inline constexpr std::size_t kReleaseSize = 16;
/** Leave carries the release fields and one trailing scalar in exactly 20 bytes. */
inline constexpr std::size_t kLeaveSize = 20;
/** Connectivity failure is 66 meaningful bits, padded to 9 bytes. */
inline constexpr std::size_t kConnectivityFailureSize = 9;
/** Connectivity failure has 66 meaningful schema bits. */
inline constexpr std::size_t kConnectivityFailureBits = 66;
/** Migration carries a biased value and a peer key in exactly 12 bytes. */
inline constexpr std::size_t kMigrationSize = 12;

/** The connectivity reason selector is two bits wide. */
inline constexpr std::uint8_t kFailureReasonWidth = 2;
/** Stored connectivity reasons are one above their logical value. */
inline constexpr std::int8_t kFailureReasonBias = 1;
/** The two-bit biased connectivity reason begins at -1. */
inline constexpr std::int8_t kMinimumFailureReason = -1;
/** The two-bit biased connectivity reason ends at 2. */
inline constexpr std::int8_t kMaximumFailureReason = 2;
/** A valid speculative migration can report no bubble with index -1. */
inline constexpr std::int32_t kMinimumMigrationBubble = -1;
/** This build has 64 bubble indices, ending at 63. */
inline constexpr std::int32_t kMaximumMigrationBubble = 63;

/**
 * One returned peer reservation.
 * The reservation context is known; what the two scalars mean on their own is not, so they
 * keep their wire order and no meaning is assigned to either.
 */
struct ReservationRelease {
    std::uint32_t scalarA{};
    std::uint32_t scalarB{};
    /** Eight key bytes, low byte first, naming the peer inside the session's ledger. */
    std::uint64_t peerKey{};
};

/** One peer leave notice from the client's own membership row. */
struct PeerLeave {
    /** The sender never writes this first field, so it remains unnamed. */
    std::uint32_t field0{};
    std::uint32_t membershipRevision{};
    std::uint64_t ownMemberKey{};
    std::uint32_t leaveReasonHash{};
};

/** One reported failure to reach a peer. */
struct ConnectivityFailure {
    std::uint64_t peerKey{};
    /** The exact logical selector is known; individual values remain unnamed. */
    std::int8_t failureReason{};
};

/** One speculative region-residency migration report. */
struct MigrationProposal {
    std::int32_t bubbleIndex{};
    /** A group member's machine key. Which member is unresolved. */
    std::uint64_t memberKey{};
};

/**
 * Parses a reservation release.
 * @param input Activity payload after the envelope.
 * @param release Cleared first, then filled.
 * @param consumedBits Cleared first, then receives 128 on success.
 * @return True only when the exact fixed body was present.
 */
[[nodiscard]] bool parse_release(std::span<const std::byte> input,
                                 ReservationRelease& release,
                                 std::size_t& consumedBits) noexcept;

/**
 * Parses a peer leave notice.
 * @param input Activity payload after the envelope.
 * @param leave Cleared first, then filled.
 * @param consumedBits Receives the bits the body used, whether or not it parsed.
 * @return True only when the exact fixed body was present.
 */
[[nodiscard]] bool
parse_leave(std::span<const std::byte> input, PeerLeave& leave, std::size_t& consumedBits) noexcept;

/**
 * Parses a connectivity failure report.
 * @param input Activity payload after the envelope.
 * @param failure Cleared first, then filled.
 * @param consumedBits Receives the bits the body used, whether or not it parsed.
 * @return True only for the exact body and zero byte padding.
 */
[[nodiscard]] bool parse_connectivity_failure(std::span<const std::byte> input,
                                              ConnectivityFailure& failure,
                                              std::size_t& consumedBits) noexcept;

/**
 * Encodes one connectivity failure for the symmetric service-9 client reader.
 * @param failure Exact peer key and logical reason in range -1..2.
 * @param output Caller-owned capacity. It changes only after full validation.
 * @param written Cleared first, then receives nine on success.
 * @return True when the reason was valid and the fixed body fitted.
 */
[[nodiscard]] bool encode_connectivity_failure(const ConnectivityFailure& failure,
                                               std::span<std::byte> output,
                                               std::size_t& written) noexcept;

/**
 * Parses a speculative migration proposal.
 * @param input Activity payload after the envelope.
 * @param proposal Cleared first, then filled.
 * @param consumedBits Receives the bits the body used, whether or not it parsed.
 * @return True only for the exact body and a bubble in range -1..63.
 */
[[nodiscard]] bool parse_migration(std::span<const std::byte> input,
                                   MigrationProposal& proposal,
                                   std::size_t& consumedBits) noexcept;

} // namespace sunrise::middleware::bap::activity_message::peer_ledger
