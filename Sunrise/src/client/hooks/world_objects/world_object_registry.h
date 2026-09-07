#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::client::hooks::world_objects {

/** The placed-entry identity value that means the entry carries none. */
constexpr std::uint64_t kAbsentPlacementIdentity = 0xFFFFFFFFFFFFFFFFULL;

/**
 * One live placed object joined to its package object-list identity.
 * `placementIdentity` is the placed-entry `+0x70` value the datum retains at `+0x90`. It is zero
 * when the datum could not be read, and `kAbsentPlacementIdentity` when the entry has none.
 */
struct Instance final {
    std::uint64_t placementIdentity{};
    std::uint32_t objectListTag{};
    std::uint32_t entryIndex{};
    std::uint32_t handle{0xFFFFFFFFU};
    std::uint32_t generation{0xFFFFFFFFU};
};

/** Bounded registry health for the technical-details surface. */
struct Diagnostics final {
    std::size_t liveCount{};
    std::uint64_t overflowCount{};
    bool installed{};
};

/** Installs the placed-object lifetime capture. */
[[nodiscard]] bool install() noexcept;

/** Removes the capture after every replacement and trampoline call is idle. */
[[nodiscard]] bool uninstall() noexcept;

/** @return True while both lifetime hooks are attached and accepting observations. */
[[nodiscard]] bool is_installed() noexcept;

/**
 * Finds generation-valid live instances for one exact package placement.
 * @param objectListTag Package object-list tag retained by the live datum.
 * @param entryIndex Exact entry in that object list.
 * @param output Receives as many simultaneous live instances as fit.
 * @return Total matching live instances, which can be greater than output.size().
 */
[[nodiscard]] std::size_t
find(std::uint32_t objectListTag, std::uint32_t entryIndex, std::span<Instance> output) noexcept;

/** @return Current bounded-registry counters. */
[[nodiscard]] Diagnostics diagnostics() noexcept;

} // namespace sunrise::client::hooks::world_objects
