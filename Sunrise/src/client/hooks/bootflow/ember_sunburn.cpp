#include <Windows.h>
#include "internal.h"
#include "../ember_movies/resources.h"
#include "../ember_movies/sunburn_rules.h"
#include "../../../core/logging/log.h"
#include <atomic>
#include <cstring>
namespace sunrise::client::hooks::bootflow {
namespace {
hooking::detour::Handle hook{};
using Attach=bool(__fastcall*)(void*,std::uint32_t);
std::atomic<unsigned> reports{};
template<class T> T read(const void* pointer) { T value{};std::memcpy(&value,pointer,sizeof(value));return value; }
std::uint32_t* ember_template(void* component) noexcept {
    __try {
        auto* self=static_cast<std::byte*>(component);
        // Only Ember's type-26 slot 43, whose source and runtime spawn template were captured live.
        if (read<std::uint32_t>(self)!=0x80B3C0C6U) return nullptr;
        const auto relative=read<std::int64_t>(self+0x210);
        if (relative<=0 || relative>0x1000) return nullptr;
        auto* request=reinterpret_cast<std::uint32_t*>(self+0x210+relative);
        return ember_movies::ember_burn_source(read<std::uint32_t>(self),read<std::uint32_t>(self+4),
            read<std::uint64_t>(self+8),*request) ? request : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
bool __fastcall attach(void* component,std::uint32_t actor) noexcept {
    const auto original=reinterpret_cast<Attach>(hook.original);
    auto* request=ember_template(component);
    if (!request) return original(component,actor);
    if (!ember_movies::sunburn_resident()) {
        if (reports.fetch_add(1)<8) core::log::write(core::log::Channel::client,core::log::Level::warn,
            "ev=ember_sunburn result=resource_unavailable asset=80B82489");
        return false;
    }
    const auto previous=*request;
    bool result{};
    // This is the writable, per-component spawn request, NOT shared package data.
    // 9F2760 -> 56DE00 consumes the asset at request+0 and creates a tracked child of this actor.
    // Preserve native duplicate checks, attachment registration and detach behavior.
    *request=0x80B82489U;
    __try { result=original(component,actor); }
    __finally { *request=previous; }
    if (result && reports.fetch_add(1)<16) core::log::writef(core::log::Channel::client,core::log::Level::info,
        "ev=ember_sunburn result=attached actor=%08X asset=80B82489",actor);
    return result;
}
}
StageResult stage_ember_sunburn(hooking::detour::Spec& spec) noexcept {
    if (hook.attached) return StageResult::attached;
    constexpr auto sig=signature<signature_length("48 89 5C 24 18 57 48 83 EC 20 8B C2 8B DA 25 FF 1F 00 00 48 8B F9 44 8B C0 8B D0 49 C1 E8 05")>(
        "48 89 5C 24 18 57 48 83 EC 20 8B C2 8B DA 25 FF 1F 00 00 48 8B F9 44 8B C0 8B D0 49 C1 E8 05");
    auto* target=scan_main_image_unique(sig,"ember_sunburn_attach");
    if (!target) return StageResult::unavailable;
    spec={target,reinterpret_cast<void*>(&attach)};return StageResult::staged;
}
void publish_ember_sunburn(const hooking::detour::Handle& value) noexcept {
    hook=value;
    core::log::write(core::log::Channel::client,core::log::Level::info,
        value.attached ? "ev=ember_sunburn result=hook_attached" : "ev=ember_sunburn result=hook_failed");
}
void uninstall_ember_sunburn() noexcept { static_cast<void>(hooking::detour::uninstall(hook)); }
}
