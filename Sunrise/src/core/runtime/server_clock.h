#pragma once

#include <chrono>
#include <cstdint>

namespace sunrise::core::runtime {

/**
 * Reads the server's own wall clock.
 * Every server-owned time field counts from the Unix epoch, so one reader serves them all.
 * @return Current time in Unix seconds.
 */
[[nodiscard]] inline std::int64_t server_clock_seconds() noexcept {
    const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(sinceEpoch).count();
}

} // namespace sunrise::core::runtime
