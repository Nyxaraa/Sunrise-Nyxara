#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../../middleware/encoding/bit_writer.h"
#include "../peer/peer_transport.h"

namespace sunrise::server::gameplay::group {

/** One reliable body staged before it is split into fragments. */
inline constexpr std::size_t kBodyCapacity = 128;
/** Room for every registry name plus its separators. */
inline constexpr std::size_t kParameterNameCapacity = 640;
/** Member index this host takes, and the index it nominates to succeed it. */
inline constexpr std::uint32_t kHostMemberIndex = 0;
/** Member index the admitted peer takes. */
inline constexpr std::uint32_t kPeerMemberIndex = 1;
/** Members one snapshot names: this host and the admitted peer. */
inline constexpr std::size_t kSnapshotMemberCount = 2;
/** Occupied-member bits of that snapshot, which the `activity-host` parameter names too. */
inline constexpr std::uint32_t kSnapshotMemberMask = (1U << kSnapshotMemberCount) - 1U;

/**
 * Sends one reliable group-session message.
 * @param sessionId Group session whose reliable channel carries it.
 * @param id Registry message id.
 * @param declaredSize Decoded structure size the registry declares.
 * @param write Callback that writes the body.
 * @return True when the message was queued.
 */
template <typename Body>
[[nodiscard]] bool send_reliable(std::uint64_t sessionId,
                                 std::uint8_t id,
                                 std::uint32_t declaredSize,
                                 Body write) noexcept {
    std::array<std::byte, kBodyCapacity> body{};
    middleware::encoding::bits::Writer writer(body);
    std::size_t size = 0;
    if (!write(writer) || !writer.finish(size)) {
        return false;
    }
    return peer::enqueue_reliable(
        sessionId, id, declaredSize, {body.data(), size}, writer.bit_count());
}

/** Encodes one sessionless reliable body for the exact peer endpoint. */
template <typename Body>
[[nodiscard]] bool send_reliable(const state::gameplay::Endpoint& endpoint,
                                 std::uint8_t id,
                                 std::uint32_t declaredSize,
                                 Body write) noexcept {
    std::array<std::byte, kBodyCapacity> body{};
    middleware::encoding::bits::Writer writer(body);
    std::size_t size = 0;
    if (!write(writer) || !writer.finish(size)) {
        return false;
    }
    return peer::enqueue_reliable(
        endpoint, id, declaredSize, {body.data(), size}, writer.bit_count());
}

} // namespace sunrise::server::gameplay::group
