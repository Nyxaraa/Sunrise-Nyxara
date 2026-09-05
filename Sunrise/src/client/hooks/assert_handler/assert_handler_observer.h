#pragma once

#include <Windows.h>

namespace sunrise::client::hooks::assert_handler {

/** @return Address of the internal assert handler body. */
[[nodiscard]] void* handler_entry_point() noexcept;

} // namespace sunrise::client::hooks::assert_handler
