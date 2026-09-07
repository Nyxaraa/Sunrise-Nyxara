#include "join_messages.h"

#include "../../encoding/bit_raw.h"

namespace sunrise::middleware::gameplay::peer {

namespace {

namespace bits = encoding::bits;

/** The protocol version is a 16-bit value field. */
constexpr std::uint8_t kProtocolWidth = 16;
/** Both build fields are 32-bit value fields. */
constexpr std::uint8_t kBuildWidth = 32;
/** The executable type is three bits. */
constexpr std::uint8_t kExecutableWidth = 3;
/** The refusal reason is six bits. */
constexpr std::uint8_t kReasonWidth = 6;
/** The peer count is five bits. */
constexpr std::uint8_t kPeerCountWidth = 5;
/** A bitstream NetAddr leads with its three-bit transport method. */
constexpr std::uint8_t kNetAddrMethodWidth = 3;
/** Methods 6 and 7 are relay transports and carry the long address form. */
constexpr std::uint64_t kFirstRelayMethod = 6;
/** Raw address bytes behind a direct method. */
constexpr std::size_t kDirectAddressBytes = 41;
/** Raw address bytes behind a relay method. */
constexpr std::size_t kRelayAddressBytes = 85;
/** The method is stored in the NetAddr's last byte. */
constexpr std::size_t kNetAddrMethodOffset = descriptor::kNetAddrSize - 1;
/** Each peer row ends with a 16-bit value and an optional five-bit slot. */
constexpr std::uint8_t kPeerRowValueWidth = 16;
/** Width of that optional slot. */
constexpr std::uint8_t kPeerRowSlotWidth = 5;
/** Presence bits are one bit. */
constexpr std::uint8_t kFlagWidth = 1;

/**
 * Reads one bitstream NetAddr into its 86-byte memory form.
 * @param reader Open reader.
 * @param output Receives the address, cleared first.
 * @return True when the method and its raw block were present.
 */
[[nodiscard]] bool read_net_addr(bits::Reader& reader,
                                 std::array<std::byte, descriptor::kNetAddrSize>& output) noexcept {
    output = {};
    std::uint64_t method = 0;
    if (!reader.read(kNetAddrMethodWidth, method)) {
        return false;
    }
    output[kNetAddrMethodOffset] = static_cast<std::byte>(method);
    const std::size_t rawBytes =
        method >= kFirstRelayMethod ? kRelayAddressBytes : kDirectAddressBytes;
    return bits::read_raw(reader, {output.data(), rawBytes});
}

/**
 * Reads one peer row: address, machine id, a 16-bit value and an optional slot.
 * Only the address and machine id are kept.
 * @param reader Open reader.
 * @param output Receives the row.
 * @return True when every field was present.
 */
[[nodiscard]] bool read_joining_peer(bits::Reader& reader, JoiningPeer& output) noexcept {
    std::uint64_t ignored = 0;
    std::uint64_t slotPresent = 0;
    if (!read_net_addr(reader, output.address) || !bits::read_raw_u64(reader, output.machineId)
        || !reader.read(kPeerRowValueWidth, ignored) || !reader.read(kFlagWidth, slotPresent)) {
        return false;
    }
    return slotPresent == 0 || reader.read(kPeerRowSlotWidth, ignored);
}

} // namespace

/** Reads the fixed fields and the peer table of a join request. */
bool read_join_request(bits::Reader& reader, JoinRequest& output) noexcept {
    std::uint64_t protocol = 0;
    std::uint64_t minimum = 0;
    std::uint64_t maximum = 0;
    std::uint64_t executable = 0;
    std::uint64_t peerCount = 0;
    JoinRequest candidate{};
    if (!reader.read(kProtocolWidth, protocol) || !reader.read(kBuildWidth, minimum)
        || !reader.read(kBuildWidth, maximum) || !reader.read(kExecutableWidth, executable)
        || !bits::read_raw_u64(reader, candidate.sessionId)
        || !bits::read_raw_u64(reader, candidate.joinId) || !reader.read(kPeerCountWidth, peerCount)
        || peerCount > kJoiningPeerCapacity) {
        return false;
    }
    for (std::size_t index = 0; index < peerCount; ++index) {
        if (!read_joining_peer(reader, candidate.peers[index])) {
            return false;
        }
    }
    candidate.protocolVersion = static_cast<std::uint16_t>(protocol);
    candidate.minimumBuild = static_cast<std::uint32_t>(minimum);
    candidate.maximumBuild = static_cast<std::uint32_t>(maximum);
    candidate.executableType = static_cast<std::uint8_t>(executable);
    candidate.peerCount = static_cast<std::uint32_t>(peerCount);
    output = candidate;
    return true;
}

/** Writes a join refusal body. */
bool write_join_refuse(bits::Writer& writer, const JoinRefuse& body) noexcept {
    return bits::write_raw_u64(writer, body.sessionId) && bits::write_raw_u64(writer, body.joinId)
           && writer.write(static_cast<std::uint64_t>(body.reason), kReasonWidth);
}

/** Reports whether a request may be answered at all. */
bool answerable(const JoinRequest& request) noexcept {
    return request.protocolVersion == kProtocolVersion;
}

/** Applies the host's admission rules in their exact order. */
bool admit(const JoinRequest& request, std::uint64_t hostSessionId, RefuseReason& reason) noexcept {
    if (!answerable(request)) {
        return false;
    }
    // This host holds one group session, so the request's session id needs no lookup.
    (void)hostSessionId;
    if (request.maximumBuild < kHostBuild) {
        reason = RefuseReason::peerVersionTooLow;
        return false;
    }
    if (request.minimumBuild > kHostBuild) {
        reason = RefuseReason::hostVersionTooLow;
        return false;
    }
    if (request.executableType != kExecutableType) {
        reason = RefuseReason::executableTypeMismatch;
        return false;
    }
    return true;
}

} // namespace sunrise::middleware::gameplay::peer
