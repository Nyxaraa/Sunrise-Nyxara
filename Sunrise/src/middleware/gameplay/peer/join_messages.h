#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "../descriptor/join_descriptor.h"

namespace sunrise::middleware::gameplay::peer {

/** Registry ids of the group-session join messages. */
enum class JoinId : std::uint8_t {
    request = 10,
    refuse = 14,
};

/** Declared decoded sizes the registry holds for those ids. Each header must carry its own. */
inline constexpr std::uint32_t kJoinRequestSize = 6144;
inline constexpr std::uint32_t kJoinRefuseSize = 24;

/** Protocol version the host requires. A mismatch is dropped with no reply at all. */
inline constexpr std::uint16_t kProtocolVersion = 0xA4F8;
/** Build the host reports. The request's build interval has to contain it. */
inline constexpr std::uint32_t kHostBuild = 86657;
/** Executable type the host requires. */
inline constexpr std::uint8_t kExecutableType = 5;

/** Refusal reasons this host emits, by their registry order. */
enum class RefuseReason : std::uint8_t {
    notFound = 4,
    peerVersionTooLow = 27,
    hostVersionTooLow = 28,
    executableTypeMismatch = 29,
};

/** Peer rows one join request can carry: the count is five bits. */
inline constexpr std::size_t kJoiningPeerCapacity = 31;

/** One row of the join request's peer table. */
struct JoiningPeer {
    /** NetAddr in memory order, the same form the connect request carries. */
    std::array<std::byte, descriptor::kNetAddrSize> address{};
    /** Stable machine identity, the value the membership's member row must carry. */
    std::uint64_t machineId{};
};

/**
 * Fixed fields and peer table of a join request.
 * The member table and the tail behind the peer table are not read.
 */
struct JoinRequest {
    std::uint16_t protocolVersion{};
    std::uint32_t minimumBuild{};
    std::uint32_t maximumBuild{};
    std::uint8_t executableType{};
    std::uint64_t sessionId{};
    /** Identifies this join attempt, not the machine. It changes on every retry, and the peer
     *  refuses a membership update that does not echo it. */
    std::uint64_t joinId{};
    std::uint32_t peerCount{};
    std::array<JoiningPeer, kJoiningPeerCapacity> peers{};
};

/** Body of a join refusal. The host echoes the request's join id. */
struct JoinRefuse {
    std::uint64_t sessionId{};
    std::uint64_t joinId{};
    RefuseReason reason{RefuseReason::notFound};
};

/**
 * Reads the fixed fields and the peer table of a join request.
 * @param reader Reader positioned at the body.
 * @param output Receives the fields admission checks.
 * @return True when every admission field was present.
 */
[[nodiscard]] bool read_join_request(encoding::bits::Reader& reader, JoinRequest& output) noexcept;

/** Writes a join refusal body. @return True when every field fit. */
[[nodiscard]] bool write_join_refuse(encoding::bits::Writer& writer,
                                     const JoinRefuse& body) noexcept;

/**
 * Applies the host's admission rules in their exact order.
 * @param request Decoded admission prefix.
 * @param hostSessionId Session id this host advertises.
 * @param reason Receives the refusal reason when admission fails.
 * @return True when the request is admitted. A protocol mismatch also returns false and leaves
 *         the reason at its default. Drop such a request without a reply.
 */
[[nodiscard]] bool
admit(const JoinRequest& request, std::uint64_t hostSessionId, RefuseReason& reason) noexcept;

/** @return True when the request may be answered at all. A protocol mismatch may not. */
[[nodiscard]] bool answerable(const JoinRequest& request) noexcept;

} // namespace sunrise::middleware::gameplay::peer
