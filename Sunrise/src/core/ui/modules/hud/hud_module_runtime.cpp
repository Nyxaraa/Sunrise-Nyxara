#include <string_view>

#include "../../hud/overlay.h"
#include "../registry/ui_module_registry.h"
#include "../ui_module_descriptor.h"
#include "hud.h"
#include "internal.h"

namespace sunrise::core::ui::modules::hud {
namespace {

/** Namespaced stable ID keeps this page distinct from feature modules. */
constexpr std::string_view kStableId = "core.hud";
/** Menu label for the on-screen overlay switches. */
constexpr std::string_view kDisplayName = "HUD";

registry::PageRegistration g_page;

} // namespace

/** Loads the saved overlay switches, then registers the page. */
bool initialize(void* module) noexcept {
    // Loaded before the page registers, so the first frame draws the saved switches.
    ui::hud::initialize(module);
    return g_page.acquire(Owner::core, kStableId, kDisplayName, &internal::draw);
}

/** Removes the Core HUD page and drops the switch file path. */
void shutdown() noexcept {
    g_page.release();
    ui::hud::shutdown();
}

} // namespace sunrise::core::ui::modules::hud
