#include "internal.h"
#include "bootflow_hook_lifecycle.h"
#include "../ember_movies/ember_movies.h"
#include "../../../core/logging/log.h"
namespace sunrise::client::hooks::bootflow {
namespace {
hooking::detour::Handle handle{};
using Tick = void(__fastcall*)(void*);
void __fastcall movie_tick(void* decoder) noexcept {
    if (auto original=reinterpret_cast<Tick>(handle.original)) original(decoder);
    // Video presentation can suspend the player-camera callback. Observe completion
    // from the movie player's own frame as well, including its final stopped frame.
    if (ember_movies::active()) poll_current_slice_set();
}
}
StageResult stage_ember_movie_tick(hooking::detour::Spec& spec) noexcept {
    if (handle.attached) return StageResult::attached;
    constexpr auto sig=signature<signature_length("4C 8B DC 55 57 49 8D AB 58 FE FF FF 48 81 EC 98 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 50 01 00 00 48 8B F9 48 8B 49 08")>(
        "4C 8B DC 55 57 49 8D AB 58 FE FF FF 48 81 EC 98 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 50 01 00 00 48 8B F9 48 8B 49 08");
    auto* target=scan_main_image_unique(sig,"ember_movie_frame");
    if (!target) return StageResult::unavailable;
    spec={target,reinterpret_cast<void*>(&movie_tick)};return StageResult::staged;
}
void publish_ember_movie_tick(const hooking::detour::Handle& value) noexcept {
    handle=value;
    ember_movies::frame_ready(value.attached);
    core::log::write(core::log::Channel::client,core::log::Level::info,
        value.attached ? "ev=ember_movie result=frame_attached" : "ev=ember_movie result=frame_attach_failed");
}
void uninstall_ember_movie_tick() noexcept {
    ember_movies::frame_ready(false);
    static_cast<void>(hooking::detour::uninstall(handle));
}
}
