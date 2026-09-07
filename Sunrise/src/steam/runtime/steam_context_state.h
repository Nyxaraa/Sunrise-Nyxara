#pragma once

namespace sunrise::steam {

/** Invalidates every caller-owned Steam context table. */
void advance_context_generation() noexcept;

} // namespace sunrise::steam
