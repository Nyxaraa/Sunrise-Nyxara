#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>

#include "busy.h"

namespace sunrise::core::ui::busy::internal {

struct Progress final {
    std::array<char, 160> detail{};
    std::uint32_t current{};
    std::uint32_t total{};
    bool available{};
    bool determinate{};
};

/** @return Mask of started tasks, one bit per Task. */
[[nodiscard]] unsigned running() noexcept;

/** @return One lock-consistent copy of a task's latest reported progress. */
[[nodiscard]] Progress progress(Task task) noexcept;

/** @param threadId Thread that draws frames, which can never wait for its own. */
void set_present_thread(DWORD threadId) noexcept;

/**
 * Records that one frame drew the overlay.
 * @param complete True when it drew at full alpha, which is what a frozen screen must carry.
 */
void record_drawn(bool complete) noexcept;

} // namespace sunrise::core::ui::busy::internal
