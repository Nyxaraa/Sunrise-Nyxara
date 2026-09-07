#include "../../../core/logging/log.h"
#include "../scripted_presentation/movies.h"
#include "bootflow_hook_lifecycle.h"
#include "internal.h"
namespace sunrise::client::hooks::bootflow {
namespace {
hooking::detour::Handle handle{};
using Tick = void(__fastcall*)(void*);
void __fastcall movie_tick(void* decoder) noexcept {
    if (auto original = reinterpret_cast<Tick>(handle.original)) original(decoder);
    // Video presentation can suspend the player-camera callback. Observe completion
    // from the movie player's own frame as well, including its final stopped frame.
    if (scripted_presentation::active()) poll_current_slice_set();
}
} // namespace
StageResult stage_movie_tick(hooking::detour::Spec& spec) noexcept {
    if (handle.attached) return StageResult::attached;
    constexpr auto sig = signature<signature_length(
        "4C 8B DC 55 57 49 8D AB 58 FE FF FF 48 81 EC 98 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 50 01 00 00 48 8B F9 48 8B 49 08")>(
        "4C 8B DC 55 57 49 8D AB 58 FE FF FF 48 81 EC 98 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 50 01 00 00 48 8B F9 48 8B 49 08");
    auto* target = scan_main_image_unique(sig, "movie_frame");
    if (!target) return StageResult::unavailable;
    spec = {target, reinterpret_cast<void*>(&movie_tick)};
    return StageResult::staged;
}
void publish_movie_tick(const hooking::detour::Handle& value) noexcept {
    handle = value;
    scripted_presentation::frame_ready(value.attached);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     value.attached ? "ev=movie result=frame_attached"
                                    : "ev=movie result=frame_attach_failed");
}
void uninstall_movie_tick() noexcept {
    scripted_presentation::frame_ready(false);
    static_cast<void>(hooking::detour::uninstall(handle));
}
} // namespace sunrise::client::hooks::bootflow
