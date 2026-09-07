#pragma once

#include <cstddef>
#include <span>

#include "../../core/logging/log.h"

namespace sunrise::server::web_service {

/**
 * Emits one formatted Server line.
 * @param level Severity for this event.
 * @param line Line storage the caller formatted into.
 * @param count snprintf result; a nonpositive value drops the line.
 */
inline void report_line(core::log::Level level, std::span<const char> line, int count) noexcept {
    if (count > 0) {
        core::log::write(
            core::log::Channel::server, level, {line.data(), static_cast<std::size_t>(count)});
    }
}

/**
 * Appends response bytes as hex behind a formatted prefix and emits the line at debug.
 * @param line Line storage holding the prefix.
 * @param prefix snprintf result; a nonpositive or overlong value drops the line.
 * @param response Encoded response body to trace.
 */
inline void report_response_line(std::span<char> line,
                                 int prefix,
                                 std::span<const std::byte> response) noexcept {
    if (prefix <= 0 || static_cast<std::size_t>(prefix) >= line.size()) {
        return;
    }
    std::size_t length = static_cast<std::size_t>(prefix);
    (void)core::log::append_hex(line, length, response);
    core::log::write(core::log::Channel::server, core::log::Level::debug, {line.data(), length});
}

} // namespace sunrise::server::web_service
