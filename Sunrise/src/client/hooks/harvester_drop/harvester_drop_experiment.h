#pragma once

namespace sunrise::client::hooks::harvester_drop {
// Experimental model doors channel: open during native delivery, close on completion.
[[nodiscard]] bool install_exit() noexcept;
bool install() noexcept;
}
