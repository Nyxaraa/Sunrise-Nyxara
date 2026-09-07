#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace sunrise::client::content::activity::scriptables {

namespace internal {

/** Builds a collision-free cache key from the two fields used by descriptor validation. */
[[nodiscard]] constexpr std::uint64_t analysis_key(std::uint32_t objectTag,
                                                   std::uint32_t registryKey) noexcept {
    return (static_cast<std::uint64_t>(objectTag) << std::numeric_limits<std::uint32_t>::digits)
           | registryKey;
}

} // namespace internal

/** Allows selected-scenario extraction requests. */
void activate() noexcept;

/**
 * Selects one installed scenario for transient extraction.
 * Repeating the same request is free unless `force` is true.
 */
[[nodiscard]] bool
request(std::uint32_t scenarioTag, std::string_view scenarioName, bool force = false) noexcept;

/** Starts or reaps the package-reader thread without blocking the caller on package IO. */
void service() noexcept;

/** Stops the reader, closes its package files, and withdraws the transient snapshot. */
void reset() noexcept;

} // namespace sunrise::client::content::activity::scriptables
