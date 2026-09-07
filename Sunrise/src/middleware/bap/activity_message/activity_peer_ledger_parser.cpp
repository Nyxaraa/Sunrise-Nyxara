/** Fixed peer-ledger bodies return structural data, never a membership or migration decision. */

#include <algorithm>
#include <array>
#include <bit>

#include "../../encoding/bit_reader.h"
#include "../../encoding/byte_order.h"
#include "peer_ledger.h"

namespace sunrise::middleware::bap::activity_message::peer_ledger {
namespace {

/** The migration scalar is biased by the midpoint of the unsigned 32-bit range. */
constexpr std::uint32_t kMigrationScalarBias = 0x80000000U;

/** Layout shared by the release body and the leave body that extends it. */
struct LedgerLayout final {
    /** The first opaque scalar starts the body. */
    static constexpr std::size_t scalarA = 0;
    /** The second opaque scalar follows the first. */
    static constexpr std::size_t scalarB = scalarA + encoding::kU32Size;
    /** Eight key bytes, low byte first, in the order the join request writes its member key. */
    static constexpr std::size_t peerKey = scalarB + encoding::kU32Size;
    /** Only the leave body carries this trailing scalar. */
    static constexpr std::size_t scalarC = peerKey + encoding::kU64Size;
};

/** Layout of the migration report. */
struct MigrationLayout final {
    /** The biased bubble index starts the body. */
    static constexpr std::size_t bubble = 0;
    /** Eight key bytes, low byte first, follow the scalar. */
    static constexpr std::size_t memberKey = bubble + encoding::kU32Size;
};

/**
 * Fills the two scalars and the peer key shared by the release and leave bodies.
 * @param input Payload holding at least the shared prefix.
 * @param scalarA Receives the first scalar.
 * @param scalarB Receives the second scalar.
 * @param peerKey Receives the peer key.
 */
void read_shared_prefix(std::span<const std::byte> input,
                        std::uint32_t& scalarA,
                        std::uint32_t& scalarB,
                        std::uint64_t& peerKey) noexcept {
    scalarA = encoding::read_u32_be(input.subspan<LedgerLayout::scalarA, encoding::kU32Size>());
    scalarB = encoding::read_u32_be(input.subspan<LedgerLayout::scalarB, encoding::kU32Size>());
    peerKey = encoding::read_u64_le(input.subspan<LedgerLayout::peerKey, encoding::kU64Size>());
}

/** Reads one struct-order eight-byte identity from an unaligned bit reader. */
[[nodiscard]] bool read_peer_key(encoding::bits::Reader& reader, std::uint64_t& value) noexcept {
    value = 0;
    for (std::size_t index = 0; index < encoding::kU64Size; ++index) {
        std::uint64_t byte = 0;
        if (!reader.read(encoding::kBitsPerByte, byte)) {
            value = 0;
            return false;
        }
        value |= byte << (index * encoding::kBitsPerByte);
    }
    return true;
}

} // namespace

/** Parses a reservation release. */
bool parse_release(std::span<const std::byte> input,
                   ReservationRelease& release,
                   std::size_t& consumedBits) noexcept {
    release = {};
    consumedBits = 0;
    if (input.size() != kReleaseSize) {
        return false;
    }
    read_shared_prefix(input, release.scalarA, release.scalarB, release.peerKey);
    consumedBits = kReleaseSize * encoding::kBitsPerByte;
    return true;
}

/** Parses a peer leave notice. */
bool parse_leave(std::span<const std::byte> input,
                 PeerLeave& leave,
                 std::size_t& consumedBits) noexcept {
    leave = {};
    consumedBits = 0;
    if (input.size() != kLeaveSize) {
        return false;
    }
    read_shared_prefix(input, leave.field0, leave.membershipRevision, leave.ownMemberKey);
    leave.leaveReasonHash =
        encoding::read_u32_be(input.subspan<LedgerLayout::scalarC, encoding::kU32Size>());
    consumedBits = kLeaveSize * encoding::kBitsPerByte;
    return true;
}

/** Parses a connectivity failure report. */
bool parse_connectivity_failure(std::span<const std::byte> input,
                                ConnectivityFailure& failure,
                                std::size_t& consumedBits) noexcept {
    failure = {};
    consumedBits = 0;
    if (input.size() != kConnectivityFailureSize) {
        return false;
    }
    encoding::bits::Reader reader(input);
    ConnectivityFailure parsed{};
    std::uint64_t reason = 0;
    if (!read_peer_key(reader, parsed.peerKey) || !reader.read(kFailureReasonWidth, reason)) {
        return false;
    }
    parsed.failureReason =
        static_cast<std::int8_t>(static_cast<std::int8_t>(reason) - kFailureReasonBias);
    std::uint64_t padding = 0;
    if (!reader.read(static_cast<std::uint8_t>(reader.remaining_bits()), padding) || padding != 0
        || reader.remaining_bits() != 0) {
        return false;
    }
    failure = parsed;
    consumedBits = kConnectivityFailureBits;
    return true;
}

/** Encodes one connectivity failure for the symmetric service-9 client reader. */
bool encode_connectivity_failure(const ConnectivityFailure& failure,
                                 std::span<std::byte> output,
                                 std::size_t& written) noexcept {
    written = 0;
    if (failure.failureReason < kMinimumFailureReason
        || failure.failureReason > kMaximumFailureReason
        || output.size() < kConnectivityFailureSize) {
        return false;
    }
    std::array<std::byte, kConnectivityFailureSize> body{};
    for (std::size_t index = 0; index < encoding::kU64Size; ++index) {
        body[index] =
            static_cast<std::byte>((failure.peerKey >> (index * encoding::kBitsPerByte)) & 0xFFU);
    }
    const auto storedReason = static_cast<std::uint8_t>(failure.failureReason + kFailureReasonBias);
    body.back() =
        static_cast<std::byte>(storedReason << (encoding::kBitsPerByte - kFailureReasonWidth));
    std::copy(body.begin(), body.end(), output.begin());
    written = body.size();
    return true;
}

/** Parses a speculative migration proposal. */
bool parse_migration(std::span<const std::byte> input,
                     MigrationProposal& proposal,
                     std::size_t& consumedBits) noexcept {
    proposal = {};
    consumedBits = 0;
    if (input.size() != kMigrationSize) {
        return false;
    }
    const std::uint32_t raw =
        encoding::read_u32_be(input.subspan<MigrationLayout::bubble, encoding::kU32Size>());
    const std::int32_t bubble = std::bit_cast<std::int32_t>(raw - kMigrationScalarBias);
    if (bubble < kMinimumMigrationBubble || bubble > kMaximumMigrationBubble) {
        return false;
    }
    proposal.bubbleIndex = bubble;
    proposal.memberKey =
        encoding::read_u64_le(input.subspan<MigrationLayout::memberKey, encoding::kU64Size>());
    consumedBits = kMigrationSize * encoding::kBitsPerByte;
    return true;
}

} // namespace sunrise::middleware::bap::activity_message::peer_ledger
