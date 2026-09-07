#pragma once

#include "definition.h"

namespace sunrise::state::activity_sdk::generation {

/** @return One lock-safe copy of the current generation job. */
[[nodiscard]] Snapshot snapshot() noexcept;

/** @return Stable text for one generation state. */
[[nodiscard]] const char* status_name(Status value) noexcept;

} // namespace sunrise::state::activity_sdk::generation
