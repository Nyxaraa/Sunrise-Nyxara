#pragma once

#include "../../../patterns/image_scan.h"

namespace sunrise::client::hooks::network::investment {

using patterns::resolve_relative;
using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/**
 * Attaches the family-five commit rearm.
 * @return True when the target is found and the detour attaches.
 */
[[nodiscard]] bool install_family5_rearm() noexcept;

/** @return True when the family-five commit rearm is absent. */
[[nodiscard]] bool uninstall_family5_rearm() noexcept;

/** @return True while the family-five commit rearm is attached. */
[[nodiscard]] bool family5_rearm_is_installed() noexcept;

/** Moves the four Arrivals leg mods into the leg plug set. Off unless `socket_menu_routing`. */
void apply_socket_menu_routing() noexcept;

/** Reserves low-address storage before retail content occupies the compatible address domain. */
void reserve_socket_menu_routing_storage() noexcept;

/** Restores owned category fields and plug-set descriptors without recycling published storage. */
void restore_socket_menu_routing() noexcept;

/** Clears the lore presentation gates. Off unless `reveal_lore_books` is set. */
void apply_lore_visibility() noexcept;

/** Puts every value the lore visibility patch replaced back. */
void restore_lore_visibility() noexcept;

/**
 * Arms one derived-state rebuild after replicated investment state changes.
 */
void arm_derived_rebuild() noexcept;

} // namespace sunrise::client::hooks::network::investment
