#pragma once

namespace sunrise::state {
struct Family5State;
}

namespace sunrise::client::hooks::network::investment {

/** Replaces the live Family-5 override lists and invalidates their derived evaluation. */
[[nodiscard]] bool publish_live_family5(const state::Family5State& family) noexcept;

/** Notes that a committed response will replace Family 4 before the next freshness query. */
void notify_family4_publication() noexcept;

/** @return True when freshness and both real-arrival rebuild arms are attached. */
[[nodiscard]] bool install() noexcept;

/** @return True when every investment rebuild detour is absent. */
[[nodiscard]] bool uninstall() noexcept;

/** @return True while freshness and both real-arrival rebuild arms are attached. */
[[nodiscard]] bool is_installed() noexcept;

/** @return True while any investment rebuild detour still needs cleanup. */
[[nodiscard]] bool has_ownership() noexcept;

} // namespace sunrise::client::hooks::network::investment
