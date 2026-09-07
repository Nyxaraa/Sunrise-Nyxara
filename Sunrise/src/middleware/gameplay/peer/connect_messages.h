#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "../descriptor/join_descriptor.h"

namespace sunrise::middleware::gameplay::peer {

/** The connect messages carry one NetAddr. */
inline constexpr std::size_t kAddressBlobSize = descriptor::kNetAddrSize;

/** Registry ids of the connection-control messages this host implements. */
enum class ConnectId : std::uint8_t {
    ping = 0,
    pong = 1,
    packetsDiscarded = 4,
    request = 5,
    response = 6,
    refuse = 7,
    establish = 8,
    closed = 9,
    /** Bounded recovery diagnostic. It sits with the control ids rather than the session ones. */
    mayday = 42,
};

/** Declared decoded sizes the registry holds for those ids. Each header must carry its own. */
inline constexpr std::uint32_t kRequestSize = 96;
inline constexpr std::uint32_t kResponseSize = 104;
inline constexpr std::uint32_t kRefuseSize = 12;
inline constexpr std::uint32_t kEstablishSize = 8;
inline constexpr std::uint32_t kClosedSize = 12;
inline constexpr std::uint32_t kPingSize = 24;
inline constexpr std::uint32_t kPongSize = 24;
inline constexpr std::uint32_t kPacketsDiscardedSize = 4;
inline constexpr std::uint32_t kMaydaySize = 10;

/** The pong response kind is two bits wide and its largest named value is 2. */
inline constexpr std::uint8_t kPongResponseKind = 2;
/** Largest wire value of a mayday code. The logical code is the wire value minus one. */
inline constexpr std::uint16_t kMaydayCodeWireMaximum = 365;

/** Body of a ping, and of the pong that echoes its first two fields. */
struct PingBody {
    std::uint16_t sequence{};
    std::uint64_t timestamp{};
    /** Trailing one-bit field the sender chose. The pong does not carry it. */
    bool trailingFlag{};
};

/** Body of a pong. It echoes the ping and names the kind of response it is. */
struct PongBody {
    std::uint16_t sequence{};
    std::uint64_t timestamp{};
    /** Two-bit response kind. The response uses the largest named value. */
    std::uint8_t responseKind{kPongResponseKind};
};

/** Body of a recovery diagnostic. The session is the group it was raised for. */
struct MaydayBody {
    std::uint64_t sessionId{};
    /** Logical state-change reason, the wire value minus one. -1 names no reason. */
    std::int16_t code{};
};

/** Body of a connect request. */
struct ConnectRequest {
    /** Incarnation counter the sender chose. It is ordered, not random, and never all ones. */
    std::uint32_t channelId{};
    /** First sequence number the sender's channel will use. */
    std::uint32_t sequence{};
    std::array<std::byte, kAddressBlobSize> address{};
};

/** Body of a connect response. The echoed pair leads and the responder's own pair follows. */
struct ConnectResponse {
    /** Channel id the requester sent, returned unaltered. */
    std::uint32_t remoteChannelId{};
    /** Sequence the requester sent, returned unaltered. A mismatch closes the connection. */
    std::uint32_t remoteSequence{};
    /** Channel id the responder chose. */
    std::uint32_t channelId{};
    /** First sequence number the responder's channel will use. */
    std::uint32_t sequence{};
    std::array<std::byte, kAddressBlobSize> address{};
};

/** Body of a connect establish. It names both channel ids rather than sequences. */
struct ConnectEstablish {
    std::uint32_t remoteChannelId{};
    std::uint32_t channelId{};
};

/** Body of a connect refuse or a connect closed. Only the reason width differs. */
struct ConnectEnd {
    /** Channel id the requester sent. */
    std::uint32_t remoteChannelId{};
    /** Sequence the requester sent. */
    std::uint32_t remoteSequence{};
    std::uint8_t reason{};
};

/** Reads a connect request body. @return True when every field was present. */
[[nodiscard]] bool read_request(encoding::bits::Reader& reader, ConnectRequest& output) noexcept;

/** Writes a connect response body. @return True when every field fit. */
[[nodiscard]] bool write_response(encoding::bits::Writer& writer,
                                  const ConnectResponse& body) noexcept;

/** Reads a connect establish body. @return True when every field was present. */
[[nodiscard]] bool read_establish(encoding::bits::Reader& reader,
                                  ConnectEstablish& output) noexcept;

/** Writes a connect establish body. @return True when every field fit. */
[[nodiscard]] bool write_establish(encoding::bits::Writer& writer,
                                   const ConnectEstablish& body) noexcept;

/** Writes a connect refuse body. @return True when every field fit. */
[[nodiscard]] bool write_refuse(encoding::bits::Writer& writer, const ConnectEnd& body) noexcept;

/** Writes a connect closed body. @return True when every field fit. */
[[nodiscard]] bool write_closed(encoding::bits::Writer& writer, const ConnectEnd& body) noexcept;

/** Reads a connect closed body. @return True when every field was present. */
[[nodiscard]] bool read_closed(encoding::bits::Reader& reader, ConnectEnd& output) noexcept;

/** Reads a ping body. @return True when every field was present. */
[[nodiscard]] bool read_ping(encoding::bits::Reader& reader, PingBody& output) noexcept;

/** Writes a pong body. @return True when every field fit. */
[[nodiscard]] bool write_pong(encoding::bits::Writer& writer, const PongBody& body) noexcept;

/**
 * Reads a packets-discarded diagnostic.
 * @param reader Reader positioned at the body.
 * @param discarded Receives the five-bit count.
 * @return True when the count was present.
 */
[[nodiscard]] bool read_packets_discarded(encoding::bits::Reader& reader,
                                          std::uint8_t& discarded) noexcept;

/**
 * Reads a recovery diagnostic.
 * @param reader Reader positioned at the body.
 * @param output Receives the session and the decoded code.
 * @return True when both fields were present and the code is within its bound.
 */
[[nodiscard]] bool read_mayday(encoding::bits::Reader& reader, MaydayBody& output) noexcept;

} // namespace sunrise::middleware::gameplay::peer
