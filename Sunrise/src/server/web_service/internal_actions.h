#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include "../../core/logging/log.h"

namespace sunrise::server::web_service {

/**
 * Emits one formatted Server warning line.
 * @tparam Size Capacity of the line storage the caller formatted into.
 * @param line Line storage the caller formatted into.
 * @param count snprintf result; a nonpositive value drops the line.
 */
template <std::size_t Size>
void write_warning(const std::array<char, Size>& line, int count) noexcept {
    if (count <= 0) {
        return;
    }
    core::log::write(core::log::Channel::server,
                     core::log::Level::warn,
                     {line.data(), (std::min)(static_cast<std::size_t>(count), line.size() - 1U)});
}

} // namespace sunrise::server::web_service
