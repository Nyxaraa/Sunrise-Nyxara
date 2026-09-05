/**
 * Retires a vendor banner the player has answered.
 *
 * The banner is an interaction chosen by the picker, which keeps the highest-priority row its
 * retire test does not reject. Offline nothing answers that test, so a quest already taken keeps
 * being offered. This hook answers it from the list `state::vendors` keeps, and records on every
 * call which interaction the vendor is showing, since this is the only place that is readable.
 */

#include "vendor_banner_retire.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../../state/vendors/answered_interactions.h"
#include "../../hooking/detour.h"

namespace sunrise::client::hooks::vendor_banner {
namespace {

/** The picker's per-interaction retire test: `(picker state, interaction index) -> skip`. */
using RetireFn = bool(__fastcall*)(void*, std::uint16_t);

hooking::detour::Handle g_handle{};
std::atomic<RetireFn> g_original{nullptr};
std::atomic_bool g_installed{false};

/**
 * Answers the picker's retire test, skipping an interaction this vendor has already answered.
 *
 * @param self Borrowed picker state for one vendor.
 * @param interactionIndex Interaction row being tested.
 * @return True when the picker must skip the row.
 */
__declspec(noinline) bool __fastcall retired(void* self, std::uint16_t interactionIndex) noexcept {
    const RetireFn original = g_original.load(std::memory_order_acquire);
    if (self != nullptr) {
        const auto* const picker = static_cast<const std::byte*>(self);
        std::uint16_t vendorIndex = 0;
        std::uint16_t selected = 0;
        std::memcpy(&vendorIndex, picker + StateLayout::vendorIndex, sizeof vendorIndex);
        std::memcpy(&selected, picker + StateLayout::selectedInteraction, sizeof selected);
        if (vendorIndex < state::vendors::kVendorCapacity) {
            state::vendors::record_shown(vendorIndex, selected);
            if (state::vendors::is_answered(vendorIndex, interactionIndex)) {
                return true;
            }
        }
    }
    return original == nullptr ? false : original(self, interactionIndex);
}

} // namespace

/** Attaches the retire gate. */
bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    state::vendors::clear();
    std::byte* const target = scan_main_image_unique(kRetireSignature, "vendor_banner_retire");
    if (target == nullptr) {
        return false;
    }
    if (!hooking::detour::install({target, reinterpret_cast<void*>(&retired)}, g_handle)) {
        return false;
    }
    g_original.store(reinterpret_cast<RetireFn>(g_handle.original), std::memory_order_release);
    g_installed.store(true, std::memory_order_release);
    return true;
}

/** Detaches the gate and forgets every answer. */
void uninstall() noexcept {
    if (!g_installed.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    (void)hooking::detour::uninstall(g_handle);
    g_original.store(nullptr, std::memory_order_release);
    state::vendors::clear();
}

} // namespace sunrise::client::hooks::vendor_banner
