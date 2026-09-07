#pragma once

#include <array>
#include <cstdint>

namespace sunrise::state::activity_sdk::generation {

/** Generation states are stable panel-facing names. */
enum class Status : std::uint8_t {
    disabled,
    waiting,
    preparing,
    building,
    publishing,
    ready,
    failed,
    cancelled,
};

/** One immutable copy of the current full-estate SDK generation job. */
struct Snapshot final {
    std::uint64_t revision{};
    std::uint32_t current{};
    std::uint32_t total{};
    std::uint32_t scenarioTag{};
    Status status{Status::disabled};
    std::array<char, 128> detail{};
};

} // namespace sunrise::state::activity_sdk::generation
