#include "mission_retirement.h"
#include <Windows.h>
#include <array>
#include <cstring>
#include <cstdio>
#include "../../../core/logging/log.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"

namespace sunrise::client::hooks::mission_retirement {
namespace {
constexpr std::size_t kCapacity = 96;
SRWLOCK g_lock = SRWLOCK_INIT;
RequestId g_id{};
Progress g_progress{};
std::array<std::uint32_t, kCapacity> g_keys{};
std::size_t g_count{};
ULONGLONG g_started{};
const std::uint16_t* g_registryHandle{};
using Resolve = const std::uintptr_t*(__fastcall*)(std::uint16_t);
Resolve g_resolve{};
bool g_resolved{};

void report(const char* line) noexcept {
    core::log::write(core::log::Channel::client, core::log::Level::info, line);
}
void resolve() noexcept {
    if (g_resolved) return;
    g_resolved = true;
    // 4E2580's clear routine supplies the registry handle and read-only pool accessor.
    // Resolve those operands; do not call the clearing routine.
    constexpr std::string_view text =
        "48 83 EC 28 0F B7 0D ? ? ? ? 66 85 C9 74 ? E8 ? ? ? ? "
        "48 8B 08 48 85 C9 74 ? 33 C0 33 D2 89 41 08 41 B8 24 01 00 00";
    constexpr auto sig = patterns::signature<patterns::signature_length(text)>(text);
    auto* p = patterns::scan_main_image_unique(sig, "mission_retirement_registry");
    if (!p) return;
    g_registryHandle = reinterpret_cast<const std::uint16_t*>(patterns::resolve_relative(p + 7, p + 11));
    g_resolve = reinterpret_cast<Resolve>(patterns::resolve_relative(p + 17, p + 21));
}
bool sample(std::uintptr_t& owner, std::size_t& matching, std::uint32_t& count) noexcept {
    __try {
        if (!g_registryHandle || !g_resolve || !*g_registryHandle) return false;
        owner = *g_resolve(*g_registryHandle);
        if (!owner) return false;
        count = *reinterpret_cast<const std::uint32_t*>(owner + 8);
        if (count == 0 || count > 128) return false; // An empty/destroyed world is not retirement.
        matching = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto key = *reinterpret_cast<const std::uint32_t*>(owner + 20 + 24 * i);
            for (std::size_t j = 0; j < g_count; ++j)
                if (key == g_keys[j]) { ++matching; break; }
        }
        return owner == *g_resolve(*g_registryHandle)
            && count == *reinterpret_cast<const std::uint32_t*>(owner + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
Status prepare(RequestId id, std::int32_t sourceRegion,
    std::span<const std::uint32_t> keys) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (!(g_id == id)) {
        g_id = id; g_progress.owner = 0; g_count = 0; g_progress.sourceRegion = sourceRegion;
        g_started = GetTickCount64();
        g_progress.value = keys.empty() || keys.size() > kCapacity || sourceRegion < 0
            ? Status::failed : Status::baselinePending;
        if (g_progress.value != Status::failed) {
            g_count = keys.size();
            std::memcpy(g_keys.data(), keys.data(), keys.size_bytes());
        }
        std::array<char, 192> line{};
        std::snprintf(line.data(), line.size(),
            "ev=mission_retirement result=requested source=%d keys=%zu status=%u revision=%llu",
            sourceRegion, keys.size(), static_cast<unsigned>(g_progress.value),
            static_cast<unsigned long long>(id.revision));
        report(line.data());
    }
    const auto result = g_progress.value;
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}
Status status(RequestId id) noexcept {
    AcquireSRWLockShared(&g_lock);
    const auto result = g_id == id ? g_progress.value : Status::absent;
    ReleaseSRWLockShared(&g_lock);
    return result;
}
void poll(std::int32_t currentRegion) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_progress.value == Status::baselinePending || g_progress.value == Status::retiring) {
        resolve();
        std::uintptr_t owner{}; std::size_t matching{}; std::uint32_t count{};
        if (GetTickCount64() - g_started > 120000) {
            g_progress.value = g_progress.owner ? Status::failedRetiring : Status::failed;
            report("ev=mission_retirement result=failed reason=native_cleanup_timeout");
        } else if (sample(owner, matching, count)) {
            const auto before = g_progress.value;
            g_progress.observe(currentRegion, owner, count, matching);
            if (before != g_progress.value)
                report(g_progress.value == Status::complete
                    ? "ev=mission_retirement result=native_cleanup_complete"
                    : "ev=mission_retirement result=baseline_observed");
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}
}
