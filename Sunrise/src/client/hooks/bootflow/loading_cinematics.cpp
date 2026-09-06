#include <cstdint>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../../../state/activity_sdk/runtime.h"
#include "internal.h"

namespace sunrise::client::hooks::bootflow {
namespace {
// EF44C0's travel hold calls LoadingCinematics_Suppressed at +30 (C24490 in
// the captured build). The tiny predicate alone matches two functions.
constexpr std::string_view kCallerText =
    "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 80 3D ? ? ? ? 00 "
    "48 8B F8 75 ? E8 ? ? ? ? 84 C0 75 ? E8 ? ? ? ? 84 C0 75 ?";
constexpr auto kCaller = signature<signature_length(kCallerText)>(kCallerText);
// C294B0 reads the activity index from the native group activity property. The
// loading flow itself uses this reader at E2EC30 before testing suppression.
constexpr std::string_view kActivityReaderText =
    "48 89 5C 24 ? 55 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? "
    "48 33 C4 48 89 85 ? ? ? ? 48 8B D9 E8 ? ? ? ? 4C 8B C0 33 D2 8B C2 "
    "4D 85 C0 74 ? 49 63 40 10 48 69 C8 A0 C8 01 00";
constexpr auto kActivityReader = signature<signature_length(kActivityReaderText)>(kActivityReaderText);
constexpr std::uint32_t kEmberDefinition = 0x38F926B2U;
using ActivityReader = void(__fastcall*)(std::uint16_t*);
ActivityReader g_activityReader{};
using Predicate = bool(__fastcall*)();
hooking::detour::Handle g_handle{};

bool entering_ember() noexcept {
    if (g_activityReader == nullptr) return false;
    std::uint16_t index = 0xFFFF;
    g_activityReader(&index);
    if (index == 0xFFFF) return false;
    const auto catalog = state::activity_sdk::snapshot();
    if (catalog == nullptr) return false;
    for (const auto& activity : catalog->activities()) {
        if (activity.activityIndex == index) {
            return activity.definitionHash == kEmberDefinition;
        }
    }
    return false;
}

bool __fastcall loading_cinematics_suppressed() noexcept {
    // Read each time: retained host sessions and saved character activity values
    // can still name Ember after the client has selected orbit or another mission.
    if (core::settings::get().client.suppressLoadingCinematics && entering_ember()) {
        return true;
    }
    const auto original = reinterpret_cast<Predicate>(g_handle.original);
    return original != nullptr && original();
}
} // namespace

StageResult stage_loading_cinematics(hooking::detour::Spec& spec) noexcept {
    if (g_handle.attached) return StageResult::attached;
    g_activityReader = reinterpret_cast<ActivityReader>(
        scan_main_image_unique(kActivityReader, "loading_cinematics_activity"));
    if (g_activityReader == nullptr) return StageResult::unavailable;
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
