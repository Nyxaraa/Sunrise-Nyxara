#pragma once

#include <cstdint>
#include <string_view>

#include "definition.h"

namespace sunrise::state::activity_sdk::generation::internal {

/** Replaces the whole job snapshot and advances its revision. */
void publish(Status status,
             std::uint32_t current,
             std::uint32_t total,
             std::uint32_t scenarioTag,
             std::string_view detail) noexcept;

/** Restores the disabled empty state. */
void clear() noexcept;

} // namespace sunrise::state::activity_sdk::generation::internal
