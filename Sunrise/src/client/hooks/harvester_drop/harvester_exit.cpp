#include "harvester_drop_experiment.h"
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"

namespace sunrise::client::hooks::harvester_drop {
namespace {
std::uintptr_t base{};
hooking::detour::Handle lookupHook{}, startHook{}, stopHook{};
using Lookup = bool (__fastcall*)(std::uint32_t, const std::uint32_t*, const std::uint32_t*, int*, int*);
using Start = std::intptr_t (__fastcall*)(void*, int);
using Stop = bool (__fastcall*)(void*, int);
std::atomic<unsigned> lookupReports{};
constexpr std::uint32_t none = 0x811c9dc5;
template<class T> T read(std::uintptr_t at) noexcept {
    T v{}; std::memcpy(&v, reinterpret_cast<const void*>(at), sizeof(v)); return v;
}
std::uintptr_t resolve(std::uint32_t handle, std::uintptr_t offset = 0) noexcept {
    if (handle == 0xffffffff) return 0;
    const auto high = static_cast<std::uint32_t>(static_cast<std::int32_t>(handle) >> 13);
    const auto pool = (high & 0xffffU) & ((static_cast<std::uint64_t>(high) | 0xffc0000ULL) >> 18);
    const auto table = read<std::uintptr_t>(read<std::uintptr_t>(base + 0x2439c70));
    const auto record = table + pool * 64;
    const auto item = read<std::uintptr_t>(record + 8)
        + read<std::uint32_t>(record + 48) * static_cast<std::uintptr_t>(handle & 0x1fff);
    const auto mask = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(read<std::int32_t>(record + 52)));
    return item - (read<std::uintptr_t>(item + 8) & mask) + offset;
}
bool exit_name(std::uint32_t name) noexcept {
    return name == 0x7b0d3643 || name == 0x7d0d39a9 || name == 0x820d414b;
}
// A889E0's exact native table: group rows are 32 bytes; variant names are u32.
// Resolve the owner from the live actor metadata rather than inventing a group hash.
// This extension is deliberately limited to an unnamed Harvester exit request.
bool find_group(std::uint32_t actor, std::uint32_t variant, std::uint32_t& group) noexcept {
    __try {
        if (actor == 0xffffffff || !exit_name(variant)) return false;
        const auto record = read<std::uintptr_t>(base + 0x1f9d7f8)
            + read<std::uint32_t>(base + 0x1f9d800) * static_cast<std::uintptr_t>(actor & 0x1fff);
        const auto entity = read<std::uint32_t>(record + 76);
        if (entity == 0xffffffff) return false;
        const auto entityRecord = read<std::uintptr_t>(base + 0x1f93428)
            + read<std::uint32_t>(base + 0x1f93430) * static_cast<std::uintptr_t>(entity & 0x1fff);
        const auto source = resolve(read<std::uint32_t>(entityRecord + 76));
        if (!source || read<std::uint32_t>(source + 4) != 0x80fe22fc) return false;
        const auto metadata = resolve(read<std::uint32_t>(record + 44));
        if (!metadata) return false;
        const auto count = read<std::int64_t>(metadata + 32);
        if (count <= 0 || count > 256) return false;
        const auto rows = metadata + 56 + read<std::uintptr_t>(metadata + 40);
        unsigned matches = 0;
        for (std::int64_t i = 0; i < count; ++i) {
            const auto row = rows + 32 * i;
            const auto variants = read<std::int64_t>(row + 16);
            if (variants < 0 || variants > 256) return false;
            const auto names = row + 40 + read<std::uintptr_t>(row + 24);
            for (std::int64_t j = 0; j < variants; ++j) {
                if (read<std::uint32_t>(names + 4 * j) != variant) continue;
                group = read<std::uint32_t>(row + 12);
                ++matches;
            }
        }
        return matches == 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool __fastcall lookup(std::uint32_t actor, const std::uint32_t* group,
    const std::uint32_t* variant, int* groupIndex, int* variantIndex) noexcept {
    const auto original = reinterpret_cast<Lookup>(lookupHook.original);
    if (!group || !variant || *group != none || !exit_name(*variant))
        return original(actor, group, variant, groupIndex, variantIndex);
    std::uint32_t resolved = none;
    const bool found = find_group(actor, *variant, resolved);
    const bool ok = original(actor, found ? &resolved : group, variant, groupIndex, variantIndex);
    if (lookupReports.fetch_add(1, std::memory_order_relaxed) < 16)
        core::log::writef(core::log::Channel::client, core::log::Level::info,
            "ev=harvester_exit stage=resolve actor=%08x group=%08x action=%08x unique=%u result=%s",
            actor, resolved, *variant, found ? 1U : 0U, ok ? "resolved" : "unresolved");
    return ok;
}
std::uint32_t action_name(void* self, int index) noexcept {
    __try {
        const auto runtime = reinterpret_cast<std::uintptr_t>(self);
        if (!runtime || index < 0 || index >= read<std::int64_t>(runtime + 1264)) return none;
        const auto source = resolve(read<std::uint32_t>(runtime), read<std::uintptr_t>(runtime + 8));
        if (!source || read<std::uint32_t>(source) != 0x80fe21ce
            || read<std::uint32_t>(source + 4) != 0x808067ec) return none;
        return read<std::uint32_t>(source + 816 + read<std::uintptr_t>(source + 784) + 80 * index);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return none; }
}
std::intptr_t __fastcall start(void* self, int index) noexcept {
    const auto name = action_name(self, index);
    const auto result = reinterpret_cast<Start>(startHook.original)(self, index);
    if (name != none)
        core::log::writef(core::log::Channel::client, core::log::Level::info,
            "ev=harvester_action stage=start action=%08x index=%d active=%d",
            name, index, read<std::int32_t>(reinterpret_cast<std::uintptr_t>(self) + 1484));
    return result;
}
bool __fastcall stop(void* self, int index) noexcept {
    const auto name = action_name(self, index);
    const bool result = reinterpret_cast<Stop>(stopHook.original)(self, index);
    if (name != none)
        core::log::writef(core::log::Channel::client, core::log::Level::info,
            "ev=harvester_action stage=stop action=%08x index=%d result=%u", name, index, result ? 1U : 0U);
    return result;
}
}
bool install_exit() noexcept {
    if (lookupHook.attached && startHook.attached && stopHook.attached) return true;
    base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    // Exact loaded-build prologues; no patching on an unrecognized executable.
    constexpr unsigned char lookupPrefix[]{0x48,0x83,0xec,0x38,0x4c,0x8b,0xd2,0x81,0xe1,0xff,0x1f,0x00,0x00};
    constexpr unsigned char startPrefix[]{0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x55,0x41,0x57,0x48,0x8d,0x6c,0x24,0xc9,0x48,0x81,0xec};
    constexpr unsigned char stopPrefix[]{0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x70,0x44,0x8b,0x11,0x48,0x8b,0xf9,0x48,0x8b,0x05,0x99};
    if (!base || std::memcmp(reinterpret_cast<void*>(base + 0xa054c0), lookupPrefix, sizeof(lookupPrefix))
        || std::memcmp(reinterpret_cast<void*>(base + 0x1023f00), startPrefix, sizeof(startPrefix))
        || std::memcmp(reinterpret_cast<void*>(base + 0x10245c0), stopPrefix, sizeof(stopPrefix))) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
            "ev=harvester_exit stage=install result=build_mismatch");
        return false;
    }
    const bool a = lookupHook.attached || hooking::detour::install(
        {reinterpret_cast<void*>(base + 0xa054c0), reinterpret_cast<void*>(&lookup)}, lookupHook);
    const bool b = startHook.attached || hooking::detour::install(
        {reinterpret_cast<void*>(base + 0x1023f00), reinterpret_cast<void*>(&start)}, startHook);
    const bool c = stopHook.attached || hooking::detour::install(
        {reinterpret_cast<void*>(base + 0x10245c0), reinterpret_cast<void*>(&stop)}, stopHook);
    core::log::writef(core::log::Channel::client, core::log::Level::info,
        "ev=harvester_exit stage=install lookup=%u start=%u stop=%u", a ? 1U : 0U, b ? 1U : 0U, c ? 1U : 0U);
    return a && b && c;
}
}
