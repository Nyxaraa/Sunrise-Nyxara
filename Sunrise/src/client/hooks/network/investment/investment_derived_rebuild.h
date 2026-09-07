#pragma once

namespace sunrise::client::hooks::network::investment {

/** Arms one derived-state rebuild on a committed investment publication. */
void notify_investment_publication() noexcept;

/** @return True when freshness and both real-arrival rebuild arms are attached. */
[[nodiscard]] bool install() noexcept;

/** @return True when every investment rebuild detour is absent. */
[[nodiscard]] bool uninstall() noexcept;

/** @return True while freshness and both real-arrival rebuild arms are attached. */
[[nodiscard]] bool is_installed() noexcept;

/** @return True while any investment rebuild detour still needs cleanup. */
[[nodiscard]] bool has_ownership() noexcept;

} // namespace sunrise::client::hooks::network::investment
