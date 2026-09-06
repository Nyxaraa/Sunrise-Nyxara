#include "internal.h"
#include "../ember_movies/ember_movies.h"
#include "../../../core/logging/log.h"
namespace sunrise::client::hooks::bootflow {
namespace {
hooking::detour::Handle handle{};
using DrawLayer=void(__fastcall*)(void*,void*,void*,int,bool,bool*);
void __fastcall draw_layer(void* ui,void* frame,void* commands,int layer,bool movie,bool* modal) noexcept {
    // 132B890 submits letterbox + native video command 1B before these UI layers.
    // Keep that renderer intact, but omit gameplay windows/HUD/fades over our movie.
    // This is drawing only: window updates/ownership remain native for restoration.
    if (ember_movies::presenting()) return;
    if (auto original=reinterpret_cast<DrawLayer>(handle.original))
        original(ui,frame,commands,layer,movie,modal);
}
}
StageResult stage_ember_movie_ui(hooking::detour::Spec& spec) noexcept {
    if (handle.attached) return StageResult::attached;
    constexpr auto sig=signature<signature_length("48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 B8 FA FF FF 48 81 EC 08 06 00 00 0F 29 70 A8 0F 29 78 98 44 0F 29 40 88")>(
        "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 B8 FA FF FF 48 81 EC 08 06 00 00 0F 29 70 A8 0F 29 78 98 44 0F 29 40 88");
    auto* target=scan_main_image_unique(sig,"ember_movie_ui_layers");
    if (!target) return StageResult::unavailable;
    spec={target,reinterpret_cast<void*>(&draw_layer)};return StageResult::staged;
}
void publish_ember_movie_ui(const hooking::detour::Handle& value) noexcept {
    handle=value;ember_movies::ui_ready(value.attached);
    core::log::write(core::log::Channel::client,core::log::Level::info,
        value.attached ? "ev=ember_movie result=ui_attached" : "ev=ember_movie result=ui_attach_failed");
}
void uninstall_ember_movie_ui() noexcept {
    ember_movies::ui_ready(false);
    static_cast<void>(hooking::detour::uninstall(handle));
}
}
