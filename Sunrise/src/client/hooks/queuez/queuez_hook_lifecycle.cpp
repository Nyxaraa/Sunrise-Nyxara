#include "queuez_hook_lifecycle.h"

#include "internal.h"

namespace sunrise::client::hooks::queuez {

/** Attaches the queuez fixes. */
bool install() noexcept {
    return install_null_payload_guard();
}

/** Detaches every queuez fix, in the reverse order of install. */
void uninstall() noexcept {
    uninstall_null_payload_guard();
}

/** @return True while at least one queuez fix is attached. */
bool is_installed() noexcept {
    return null_payload_guard_installed();
}

} // namespace sunrise::client::hooks::queuez
