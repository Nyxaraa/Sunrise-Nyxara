#include "harvester_drop_experiment.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../../hooking/detour.h"

namespace sunrise::client::hooks::harvester_drop {
namespace {
hooking::detour::Handle g_hook{};
std::uintptr_t g_base{};
using SetStage = std::intptr_t (__fastcall*)(void*, int);
using SetChannel = std::intptr_t (__fastcall*)(std::uint32_t, const std::uint32_t*, const float*);
// 80FE22CB source+0x1F0 names the model channel; FNV-1("doors").
constexpr std::uint32_t kDoorsChannel = 0x80296344;

template<class T> T read(std::uintptr_t address) noexcept {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

// Resolve the source exactly as 1029330 does. The runtime component is a separate
// allocation: offsets into an entity's source slab are not runtime pointers.
bool is_harvester_delivery(std::uintptr_t runtime) noexcept {
    __try {
        const auto handle = read<std::uint32_t>(runtime);
        if (handle == 0xffffffff) return false;
        const auto high = static_cast<std::uint32_t>(static_cast<std::int32_t>(handle) >> 13);
        const auto pool = (high & 0xffffU) & ((static_cast<std::uint64_t>(high) | 0xffc0000ULL) >> 18);
        const auto table = read<std::uintptr_t>(read<std::uintptr_t>(g_base + 0x2439C70));
        const auto record = table + pool * 64;
        const auto item = read<std::uintptr_t>(record + 8)
            + read<std::uint32_t>(record + 48) * static_cast<std::uintptr_t>(handle & 0x1fff);
        const auto mask = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(read<std::int32_t>(record + 52)));
        const auto slab = item - (read<std::uintptr_t>(item + 8) & mask);
        const auto source = slab + read<std::uintptr_t>(runtime + 8);
        // The native reference resolves the delivery RESOURCE, not the owning
        // actor's source slab. Its root is a resource header (size/offsets), so
        // checking slab+4 for the actor tag rejected every real delivery.
        return read<std::uint32_t>(source) == 0x80C0E5D2
            && read<std::uint32_t>(source + 4) == 0x8080670A;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The live capture proved all four delivery action bindings are empty. Drive the
// separately authored model channel instead, using the native named-scalar route
// used by actor command A9BFA0. Match only this delivery resource, never other AI.
void set_doors(void* self, int stage) noexcept {
    const auto runtime = reinterpret_cast<std::uintptr_t>(self);
    if (!self || (stage != 0 && stage != 1) || !is_harvester_delivery(runtime)) return;
    __try {
        const auto entity = read<std::uint32_t>(runtime + 44);
        const auto priorStage = read<std::uint8_t>(runtime + 480);
        if (entity == 0xffffffff || priorStage == stage) return;
        const float value = stage == 1 ? 1.0f : 0.0f;
        alignas(16) const float values[4]{value, value, value, value};
        reinterpret_cast<SetChannel>(g_base + 0x576420)(entity, &kDoorsChannel, values);
        core::log::writef(core::log::Channel::client, core::log::Level::info,
            "ev=harvester_doors stage=%d previous=%u entity=%08x channel=%08x value=%.1f result=requested",
            stage, static_cast<unsigned>(priorStage), entity, kDoorsChannel, static_cast<double>(value));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        core::log::write(core::log::Channel::client, core::log::Level::error,
            "ev=harvester_doors result=exception");
    }
}

std::intptr_t __fastcall set_stage(void* self, int stage) noexcept {
    set_doors(self, stage);
    return reinterpret_cast<SetStage>(g_hook.original)(self, stage);
}
}

bool install() noexcept {
    if (g_hook.attached) return install_exit();
    if (!core::settings::get().server.activation.missionScripting) return true;
    g_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    constexpr unsigned char stagePrefix[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,
        0x48,0x63,0xda,0x48,0x8b,0xf9,0x88,0x99,0xe0,0x01,0x00,0x00};
    constexpr unsigned char channelPrefix[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
        0x55,0x57,0x41,0x56,0x48,0x8d,0xac,0x24,0x70,0xf4,0xff,0xff,0x48,0x81,0xec,0x90,0x0c,0x00,0x00};
    if (!g_base || std::memcmp(reinterpret_cast<void*>(g_base + 0x102A5B0), stagePrefix, sizeof(stagePrefix))
        || std::memcmp(reinterpret_cast<void*>(g_base + 0x576420), channelPrefix, sizeof(channelPrefix))) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
            "ev=harvester_doors stage=install result=build_mismatch");
        return false;
    }
    const bool ok = hooking::detour::install({reinterpret_cast<void*>(g_base + 0x102A5B0),
        reinterpret_cast<void*>(&set_stage)}, g_hook);
    core::log::writef(core::log::Channel::client, core::log::Level::info,
        "ev=harvester_doors stage=install result=%s", ok ? "ok" : "failed");
    const bool exitOk = install_exit();
    return ok && exitOk;
}
}
