#include "server_ui_module_runtime.h"

#include <string_view>

#include "../../../core/ui/modules/registry/ui_module_registry.h"
#include "../../../core/ui/modules/ui_module_descriptor.h"
#include "../activity_host/activity_host_panel.h"
#include "../activity_override/activity_override_panel.h"

namespace sunrise::server::ui::runtime {
namespace {

/** A namespaced stable ID keeps Server modules from clashing with Client modules. */
constexpr std::string_view kOverrideStableId = "server.activity_override";
/** Short menu label for the activity override page. */
constexpr std::string_view kOverrideDisplayName = "Activity";
/** A namespaced stable ID for the Activity Host page. */
constexpr std::string_view kHostStableId = "server.activity_host";
/** Short menu label for the Activity Host page. */
constexpr std::string_view kHostDisplayName = "Activity Host";

core::ui::modules::registry::PageRegistration g_overridePage;
core::ui::modules::registry::PageRegistration g_hostPage;

} // namespace

/** @return True when the Server module owns its Core UI registry slot. */
bool initialize() noexcept {
    if (!g_overridePage.acquire(core::ui::modules::Owner::server,
                                kOverrideStableId,
                                kOverrideDisplayName,
                                &activity_override::draw)) {
        return false;
    }
    if (!g_hostPage.acquire(core::ui::modules::Owner::server,
                            kHostStableId,
                            kHostDisplayName,
                            &activity_host::draw,
                            nullptr,
                            &activity_host::draw_windows)) {
        g_overridePage.release();
        return false;
    }
    return true;
}

/** Removes the Server module from the Core UI registry. */
void shutdown() noexcept {
    g_hostPage.release();
    g_overridePage.release();
}

} // namespace sunrise::server::ui::runtime
