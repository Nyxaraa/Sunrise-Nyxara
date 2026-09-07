#pragma once

#include <cstddef>

namespace sunrise::server::transport {

/** Coalesced frames one connection may drain per service slice before others get the thread. */
inline constexpr std::size_t kFrameBatchLimit = 16;

/**
 * Drains coalesced TCP frames one at a time, flushing each response before the next frame so
 * an unsent response is never overwritten. Stops when the stream does not shrink, which means
 * the next frame is incomplete.
 * @return False when the drain or flush failed and the peer must close.
 */
template <class Peer, class Drain, class Flush>
bool drain_frame_batch(Peer& peer, Drain drain, Flush flush) {
    for (std::size_t count = 0; count < kFrameBatchLimit && peer.outputSize == 0; ++count) {
        const auto before = peer.streamSize;
        if (!drain(peer)) {
            return false;
        }
        if (before == peer.streamSize) {
            break;
        }
        if (peer.outputSize != 0 && !flush(peer)) {
            return false;
        }
    }
    return true;
}

} // namespace sunrise::server::transport
