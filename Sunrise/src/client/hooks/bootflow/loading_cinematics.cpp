#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "internal.h"

namespace sunrise::client::hooks::bootflow {
namespace {
// EF44C0's travel hold calls LoadingCinematics_Suppressed at +30 (C24490 in
// the captured build). The tiny predicate alone matches two functions.
constexpr std::string_view kCallerText =
    "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 80 3D ? ? ? ? 00 "
    "48 8B F8 75 ? E8 ? ? ? ? 84 C0 75 ? E8 ? ? ? ? 84 C0 75 ?";
constexpr auto kCaller = signature<signature_length(kCallerText)>(kCallerText);
using Predicate = bool(__fastcall*)();
hooking::detour::Handle g_handle{};

bool __fastcall loading_cinematics_suppressed() noexcept {
    if (core::settings::get().client.suppressLoadingCinematics) {
        return true;
    }
    const auto original = reinterpret_cast<Predicate>(g_handle.original);
    return original != nullptr && original();
}
} // namespace

StageResult stage_loading_cinematics(hooking::detour::Spec& spec) noexcept {
    if (g_handle.attached) return StageResult::attached;
    const auto* caller = scan_main_image_unique(kCaller, "loading_cinematics_hold");
    if (caller == nullptr) return StageResult::unavailable;
    auto* target = resolve_relative(caller + 31, caller + 35);
    // Validate the no-argument boolean wrapper before staging its detour.
    constexpr unsigned char head[]{0x48, 0x83, 0xEC, 0x28, 0xE8};
    constexpr unsigned char tail[]{0x84, 0xC0, 0x0F, 0x95, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3};
    if (target == nullptr || std::memcmp(target, head, sizeof(head)) != 0
        || std::memcmp(target + 9, tail, sizeof(tail)) != 0) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
                        "ev=bootflow stage=loading_cinematics result=fail reason=predicate_signature");
        return StageResult::unavailable;
    }
    spec = hooking::detour::Spec{target, reinterpret_cast<void*>(&loading_cinematics_suppressed)};
    return StageResult::staged;
}

void publish_loading_cinematics(const hooking::detour::Handle& handle) noexcept {
    g_handle = handle;
    core::log::write(core::log::Channel::client,
                    handle.attached ? core::log::Level::info : core::log::Level::warn,
                    handle.attached ? "ev=bootflow stage=loading_cinematics result=attached"
                                    : "ev=bootflow stage=loading_cinematics result=fail reason=attach");
}

void uninstall_loading_cinematics() noexcept {
    if (g_handle.attached) (void)hooking::detour::uninstall(g_handle);
}
} // namespace sunrise::client::hooks::bootflow
