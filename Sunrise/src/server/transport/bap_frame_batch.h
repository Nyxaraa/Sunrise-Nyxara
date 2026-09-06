#pragma once
#include <cstddef>

namespace sunrise::server::transport {
// Drain coalesced TCP frames without overwriting an unsent response. The budget
// keeps one busy connection from monopolizing the server thread.
template<class Peer, class Drain, class Flush>
bool drain_frame_batch(Peer& peer, Drain drain, Flush flush) {
    for (std::size_t count = 0; count < 16 && peer.outputSize == 0; ++count) {
        const auto before = peer.streamSize;
        if (!drain(peer)) return false;
        if (before == peer.streamSize) break; // Incomplete frame: wait for more bytes.
        if (peer.outputSize != 0 && !flush(peer)) return false;
    }
    return true;
}
}
