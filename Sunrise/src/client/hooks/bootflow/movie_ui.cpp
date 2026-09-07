#include "../../../core/logging/log.h"
#include "../scripted_presentation/movies.h"
#include "../scripted_presentation/playback_rules.h"
#include "internal.h"
namespace sunrise::client::hooks::bootflow {
namespace {
hooking::detour::Handle handle{};
using SelectState = bool(__fastcall*)(void*, int);
bool __fastcall select_state(void* ui, int requested) noexcept {
    // Use the playback window, not the loading branch of E2EAE0.
    // 1312540/13126E0 map 21h to cinematic_overlay and 22h to loading.
    // The direct player has no type-6 controller to advance that branch.
    // E1CD60 owns window/category transitions; all drawing stays native.
    // Preserve loading/error/menu requests; substitute only normal gameplay.
    const int selected =
        scripted_presentation::movie_ui_state(requested, scripted_presentation::presenting());
    auto original = reinterpret_cast<SelectState>(handle.original);
    if (!original) return false;
    const bool result = original(ui, selected);
    static bool wasCinematic{};
    const bool cinematic = selected != requested;
    if (cinematic != wasCinematic) {
        core::log::writef(
            core::log::Channel::client,
            core::log::Level::info,
            "ev=movie result=presentation_state requested=%d selected=%d applied=%d category=%d",
            requested,
            selected,
            *reinterpret_cast<int*>(static_cast<std::byte*>(ui) + 0x204),
            *reinterpret_cast<int*>(static_cast<std::byte*>(ui) + 0x200));
        wasCinematic = cinematic;
    }
    return result;
}
} // namespace
StageResult stage_movie_ui(hooking::detour::Spec& spec) noexcept {
    if (handle.attached) return StageResult::attached;
    constexpr auto sig = signature<signature_length(
        "40 55 53 57 41 54 41 55 41 57 48 8D AC 24 08 FD FF FF 48 81 EC F8 03 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 E0 02 00 00 4C 8B E9 8B DA")>(
        "40 55 53 57 41 54 41 55 41 57 48 8D AC 24 08 FD FF FF 48 81 EC F8 03 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 E0 02 00 00 4C 8B E9 8B DA");
    auto* target = scan_main_image_unique(sig, "movie_presentation_state");
    if (!target) return StageResult::unavailable;
    spec = {target, reinterpret_cast<void*>(&select_state)};
    return StageResult::staged;
}
void publish_movie_ui(const hooking::detour::Handle& value) noexcept {
    handle = value;
    scripted_presentation::ui_ready(value.attached);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     value.attached ? "ev=movie result=presentation_attached"
                                    : "ev=movie result=presentation_attach_failed");
}
void uninstall_movie_ui() noexcept {
    scripted_presentation::ui_ready(false);
    static_cast<void>(hooking::detour::uninstall(handle));
}
} // namespace sunrise::client::hooks::bootflow
