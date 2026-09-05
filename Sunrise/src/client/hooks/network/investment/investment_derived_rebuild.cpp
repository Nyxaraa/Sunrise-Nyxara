#include "investment_derived_rebuild.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../../../core/logging/log.h"
#include "../../../hooking/detour.h"
#include "internal.h"

namespace sunrise::client::hooks::network::investment {
namespace {

/**
 * The derived-state freshness verdict. Only a rebuild recomputes the expiry, so a character
 * cached before its replicated objects arrive never goes stale by itself.
 */
constexpr std::string_view kFreshnessSignatureText =
    "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 48 8B F8 0F B6 40 08 84 C0 74 ? 48 8B 53 18 "
    "48 8B 4B 08 E8 ? ? ? ?";
/** Compiled pattern bytes for the freshness verdict above. */
constexpr auto kFreshnessSignature =
    signature<signature_length(kFreshnessSignatureText)>(kFreshnessSignatureText);

/** The state-three family-four lookup call. A nonnull result proves the real object arrived. */
constexpr std::string_view kFamily4CallSignatureText =
    "48 8D 4B 10 E8 ? ? ? ? 48 8D B8 28 07 00 00 48 83 3F 00";
/** Compiled pattern bytes for the family-four lookup call above. */
constexpr auto kFamily4CallSignature =
    signature<signature_length(kFamily4CallSignatureText)>(kFamily4CallSignatureText);

/** Byte offset of the `E8` near call inside the matched family-four pattern. */
constexpr std::size_t kFamily4CallOperandOffset = 5;
/** An x64 near call has a 4-byte relative displacement. */
constexpr std::size_t kNearCallOperandSize = 4;
/** Detour handle slots, fixed so install and uninstall use the same pair. */
constexpr std::size_t kFreshnessHandle = 0;
constexpr std::size_t kFamily4Handle = 1;
/** The freshness verdict the game reads as "rebuild required". */
constexpr char kStale = 0;

using Freshness = char(__fastcall*)(void*);
using Family4Lookup = void*(__fastcall*)(std::uint64_t*);

std::array<hooking::detour::Handle, 2> g_handles{};
std::atomic<Freshness> g_originalFreshness{nullptr};
std::atomic<Family4Lookup> g_originalFamily4Lookup{nullptr};
std::atomic_bool g_rebuildArmed{false};
std::atomic_bool g_reportedRebuild{false};
std::atomic<void*> g_committedFamily4{nullptr};
std::atomic_uint32_t g_pendingFamily4Publications{0};

/** @return True while either primary rebuild detour is attached. */
[[nodiscard]] bool any_primary_attached() noexcept {
    return g_handles[kFreshnessHandle].attached || g_handles[kFamily4Handle].attached;
}

/** Clears call targets and the once-per-lifecycle log flags after full detach. */
void clear_runtime() noexcept {
    g_originalFreshness.store(nullptr, std::memory_order_release);
    g_originalFamily4Lookup.store(nullptr, std::memory_order_release);
    g_rebuildArmed.store(false, std::memory_order_release);
    g_reportedRebuild.store(false, std::memory_order_release);
    g_committedFamily4.store(nullptr, std::memory_order_release);
    g_pendingFamily4Publications.store(0, std::memory_order_release);
}

/**
 * Reports stale once after a real replicated-object commit.
 * @param accessor Borrowed derived-state accessor.
 * @return Stale once while armed, otherwise the native verdict.
 */
__declspec(noinline) char __fastcall freshness(void* accessor) noexcept {
    const Freshness original = g_originalFreshness.load(std::memory_order_acquire);
    // The native verdict performs the Family-4 lookup. That lookup is what arms the initial
    // rebuild, so it must run before the arm is consumed; checking first left the arm stranded
    // when sign-on made only one freshness query and every Triumph card kept its stale action.
    const char nativeVerdict = original != nullptr ? original(accessor) : kStale;
    if (g_rebuildArmed.exchange(false, std::memory_order_acq_rel)) {
        if (!g_reportedRebuild.exchange(true, std::memory_order_relaxed)) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             "ev=investment stage=derived result=rebuilt");
        }
        return kStale;
    }
    return nativeVerdict;
}

/**
 * Arms a rebuild whenever the state-three lookup observes a different committed Family-4 object.
 * The freshness verdict itself performs this lookup, so a simple "nonnull" test would re-arm on
 * every query and keep the derived state permanently stale. Object identity changes only when the
 * queuez replacement has committed, which gives initial sign-on and later account after-images the
 * same boundary without an unrelated Family-5 publication.
 * @param key Borrowed account key.
 * @return The native lookup result, unchanged.
 */
__declspec(noinline) void* __fastcall family4_lookup(std::uint64_t* key) noexcept {
    const Family4Lookup original = g_originalFamily4Lookup.load(std::memory_order_acquire);
    void* const resolved = original != nullptr ? original(key) : nullptr;
    if (resolved != nullptr) {
        std::uint32_t pending = g_pendingFamily4Publications.load(std::memory_order_acquire);
        while (pending != 0
               && !g_pendingFamily4Publications.compare_exchange_weak(
                   pending, pending - 1, std::memory_order_acq_rel, std::memory_order_acquire)) {}
        if (pending != 0) {
            arm_derived_rebuild();
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             "ev=investment stage=family4_commit result=armed source=publication");
        }
    }
    void* previous = g_committedFamily4.load(std::memory_order_acquire);
    if (resolved != nullptr && resolved != previous
        && g_committedFamily4.compare_exchange_strong(
            previous, resolved, std::memory_order_acq_rel, std::memory_order_acquire)) {
        arm_derived_rebuild();
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=investment stage=family4_commit result=armed");
    }
    return resolved;
}

} // namespace

/** Arms one derived-state rebuild, used up by the next freshness verdict. */
void arm_derived_rebuild() noexcept {
    g_rebuildArmed.store(true, std::memory_order_release);
}

/** Carries an exact committed account publication to its next native Family-4 lookup. */
void notify_family4_publication() noexcept {
    g_pendingFamily4Publications.fetch_add(1, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=investment stage=family4_publication result=pending");
}

/** @return True when freshness and both real-arrival rebuild arms are attached. */
bool install() noexcept {
    if (is_installed()) {
        return true;
    }
    if (has_ownership()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=install result=fail reason=ownership");
        return false;
    }

    std::byte* const freshnessTarget =
        scan_main_image_unique(kFreshnessSignature, "investment_derived_freshness");
    std::byte* const family4Call =
        scan_main_image_unique(kFamily4CallSignature, "queuez_family4_readiness_call");
    if (freshnessTarget == nullptr || family4Call == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=install result=fail reason=target");
        return false;
    }
    std::byte* const family4Target =
        resolve_relative(family4Call + kFamily4CallOperandOffset,
                         family4Call + kFamily4CallOperandOffset + kNearCallOperandSize);
    if (family4Target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=install result=fail reason=operand");
        return false;
    }

    const std::array specs{
        hooking::detour::Spec{freshnessTarget, reinterpret_cast<void*>(&freshness)},
        hooking::detour::Spec{family4Target, reinterpret_cast<void*>(&family4_lookup)},
    };
    std::array<hooking::detour::Handle, 2> installed{};
    if (!hooking::detour::install(specs, installed)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=install result=fail reason=attach");
        return false;
    }
    g_handles = installed;
    g_originalFreshness.store(reinterpret_cast<Freshness>(g_handles[kFreshnessHandle].original),
                              std::memory_order_release);
    g_originalFamily4Lookup.store(
        reinterpret_cast<Family4Lookup>(g_handles[kFamily4Handle].original),
        std::memory_order_release);

    if (!install_family5_rearm()) {
        if (hooking::detour::uninstall(g_handles)) {
            clear_runtime();
        } else {
            core::log::write(core::log::Channel::client,
                             core::log::Level::warn,
                             "ev=investment stage=install result=fail reason=rollback");
        }
        return false;
    }
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=investment stage=install result=ok");
    return true;
}

/** @return True when every investment rebuild detour is absent. */
bool uninstall() noexcept {
    restore_lore_visibility();
    restore_socket_menu_routing();
    if (!uninstall_family5_rearm()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=uninstall result=fail reason=family5");
        return false;
    }
    if (any_primary_attached()
        && (!g_handles[kFreshnessHandle].attached || !g_handles[kFamily4Handle].attached
            || !hooking::detour::uninstall(g_handles))) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=uninstall result=fail reason=detach");
        return false;
    }
    clear_runtime();
    return true;
}

/** @return True while freshness and both real-arrival rebuild arms are attached. */
bool is_installed() noexcept {
    return g_handles[kFreshnessHandle].attached && g_handles[kFamily4Handle].attached
           && family5_rearm_is_installed();
}

/** @return True while any investment rebuild detour still needs cleanup. */
bool has_ownership() noexcept {
    return any_primary_attached() || family5_rearm_is_installed();
}

} // namespace sunrise::client::hooks::network::investment
