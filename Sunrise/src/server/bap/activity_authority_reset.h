#pragma once

#include <cstdint>

namespace sunrise::server::bap {

/** Stable result of one complete msg-28 authority-mask reset. */
struct ActivityAuthorityResetSnapshot final {
    std::uint64_t bindingGeneration{};
    std::int32_t correlation{-1};
    bool complete{};
};

/** Stable transport-side outcome for one connection-owned authority reset. */
enum class ActivityAuthorityResetStatus : std::uint8_t {
    queued,
    complete,
    noActivityLink,
    staleActivityClient,
    busy,
    pending,
    timedOut,
    noResult,
};

} // namespace sunrise::server::bap
