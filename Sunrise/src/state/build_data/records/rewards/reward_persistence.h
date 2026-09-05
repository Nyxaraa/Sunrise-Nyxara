#pragma once

namespace sunrise::state::build_data::records::rewards {

/** Resolves the shipped reward-table path from the loaded DLL. */
[[nodiscard]] bool initialize(void* module) noexcept;

/** Loads and publishes the table; a missing or empty file is a clean no-op. */
[[nodiscard]] bool load_and_publish() noexcept;

} // namespace sunrise::state::build_data::records::rewards
