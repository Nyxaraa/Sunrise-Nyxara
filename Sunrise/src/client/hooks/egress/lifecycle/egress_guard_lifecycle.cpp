#include <algorithm>
#include <array>
#include <cstdio>
#include <shared_mutex>

#include "../../../../core/logging/log.h"
#include "../internal.h"
#include "../runtime.h"
#include "core/threading/srw_lock.h"
#include "internal.h"

namespace sunrise::client::hooks::egress {

core::threading::SrwLock g_lock{};
std::array<hooking::detour::Handle, kHookCount> g_handles{};

namespace {

namespace lifecycle = lifecycle;

/** Export names recorded at install so the outcome can be named once logging exists. */
std::array<const char*, kHookCount> g_exportNames{};
/** Per-export result recorded at install. */
std::array<bool, kHookCount> g_exportResolved{};
bool g_batchAttached{};
/** Two exports can reach the report; only the first emits it. */
bool g_reported{};
HMODULE g_ownerModule{};
/** DnsQueryRaw is the only guarded export absent from older supported Windows builds. */
std::size_t g_activeHookCount{};

/** @return True when every export present during installation is attached. */
[[nodiscard]] bool all_installed() noexcept {
    if (g_activeHookCount < lifecycle::kRequiredHookCount || g_activeHookCount > g_handles.size()) {
        return false;
    }
    for (std::size_t index = 0; index < g_activeHookCount; ++index) {
        if (!g_handles[index].attached) {
            return false;
        }
    }
    return true;
}

/** @return True when any egress hook is attached. */
[[nodiscard]] bool any_installed() noexcept {
    return std::any_of(g_handles.begin(),
                       g_handles.end(),
                       [](const hooking::detour::Handle& handle) { return handle.attached; });
}

/**
 * Pins the replacement code because process-lifetime hooks survive Steam shutdown.
 * @return True when Windows promises the owning image cannot unload.
 */
[[nodiscard]] bool pin_owner_module() noexcept {
    if (g_ownerModule != nullptr) {
        return true;
    }
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
    return GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&install), &g_ownerModule) != FALSE;
}

} // namespace

/** Installs every resolver and socket guard in one process-wide transaction. */
bool install() noexcept {
    const std::lock_guard lock(g_lock);
    if (all_installed()) {
        return true;
    }
    if (any_installed() || !pin_owner_module() || !lifecycle::load_modules()) {
        return false;
    }

    std::array<hooking::detour::Spec, kHookCount> specs{};
    std::size_t count = 0;
    if (!lifecycle::resolve_specs(specs, g_exportNames, g_exportResolved, count)
        || !hooking::detour::install(std::span(specs.data(), count),
                                     std::span(g_handles.data(), count))) {
        g_activeHookCount = 0;
        g_batchAttached = false;
        lifecycle::release_modules();
        return false;
    }
    g_activeHookCount = count;
    g_batchAttached = true;
    return true;
}

/** Emits one line per guarded export, then the batch outcome. */
void report_installation() noexcept {
    const std::lock_guard lock(g_lock);
    if (g_reported) {
        return;
    }
    g_reported = true;
    const std::size_t count = g_activeHookCount;
    const bool attached = g_batchAttached;
    for (std::size_t index = 0; index < kHookCount; ++index) {
        if (g_exportNames[index] == nullptr) {
            continue;
        }
        const bool ready = g_exportResolved[index] && index < count && attached;
        std::array<char, 128> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=hook stage=attach group=egress name=%s result=%s",
                                          g_exportNames[index],
                                          ready ? "ok" : "fail");
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             ready ? core::log::Level::debug : core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
    std::array<char, 96> summary{};
    const int written = std::snprintf(summary.data(),
                                      summary.size(),
                                      "ev=hook stage=install group=egress count=%zu result=%s",
                                      count,
                                      attached ? "ok" : "fail");
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         attached ? core::log::Level::info : core::log::Level::error,
                         {summary.data(), static_cast<std::size_t>(written)});
    }
}

/** @return True only when every required guard detour is attached. */
bool is_installed() noexcept {
    const std::shared_lock lock(g_lock);
    const bool installed = all_installed();
    return installed;
}

} // namespace sunrise::client::hooks::egress
