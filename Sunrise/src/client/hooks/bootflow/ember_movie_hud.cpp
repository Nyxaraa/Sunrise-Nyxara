#include <cstdint>
#include <cstring>
#include <intrin.h>
#include "internal.h"
#include "../ember_movies/ember_movies.h"
#include "../../../core/logging/log.h"

namespace sunrise::client::hooks::bootflow {
namespace {
hooking::detour::Handle handle{};
const void* windowDrawReturn{};
using DrawWidget = void(__fastcall*)(void*, void*, void*);

void __fastcall draw_widget(void* widget, void* context, void* commands) noexcept {
    const bool presenting = ember_movies::presenting();
    static thread_local bool reported{};
    if (!presenting) reported = false;
    // 13D9060 also submits nested widgets. Only the verified window-list caller
    // (132C1BD) supplies a full window; never read +310 from a child widget.
    if (presenting && widget && _ReturnAddress() == windowDrawReturn) {
        std::int32_t role{};
        std::memcpy(&role, static_cast<const std::byte*>(widget) + 0x310, sizeof(role));
        // 131778E assigns role 18 through 13165C0 to the gameplay HUD, including
        // equipment-specific replacements of the default "hud" window.
        // Skip its entire cached subtree, not the layer containing subtitles,
        // menus and cinematic_overlay. Native update/input/visibility stay intact.
        if (role == 0x12) {
            if (!reported) {
                std::uint32_t asset{}, name{};
                std::memcpy(&asset, static_cast<const std::byte*>(widget) + 0x18, sizeof(asset));
                std::memcpy(&name, static_cast<const std::byte*>(widget) + 0x24, sizeof(name));
                core::log::writef(core::log::Channel::client, core::log::Level::info,
                    "ev=ember_movie result=hud_draw_suppressed role=%d asset=%08X name=%08X",
                    role, asset, name);
                reported = true;
            }
            return;
        }
    }
    if (auto original = reinterpret_cast<DrawWidget>(handle.original))
        original(widget, context, commands);
}
}

StageResult stage_ember_movie_hud(hooking::detour::Spec& spec) noexcept {
    if (handle.attached) return StageResult::attached;
    constexpr auto windowDrawSig = signature<signature_length("4C 8B C7 48 8D 54 24 70 48 8B CE E8 ? ? ? ? 48 8B CF E8 ? ? ? ?")>(
        "4C 8B C7 48 8D 54 24 70 48 8B CE E8 ? ? ? ? 48 8B CF E8 ? ? ? ?");
    constexpr auto widgetSig = signature<signature_length("40 57 41 55 41 56 48 83 EC 30 48 8B F9 4D 8B F0 0F B7 49 1E 4C 8B EA")>(
        "40 57 41 55 41 56 48 83 EC 30 48 8B F9 4D 8B F0 0F B7 49 1E 4C 8B EA");
    const auto* caller = scan_main_image_unique(windowDrawSig, "ember_movie_hud_window_call");
    auto* target = scan_main_image_unique(widgetSig, "ember_movie_hud_widget_draw");
    if (!caller || !target || resolve_relative(caller + 12, caller + 16) != target)
        return StageResult::unavailable;
    windowDrawReturn = caller + 16;
    spec = {target, reinterpret_cast<void*>(&draw_widget)};
    return StageResult::staged;
}
void publish_ember_movie_hud(const hooking::detour::Handle& value) noexcept {
    handle = value;
    core::log::write(core::log::Channel::client, core::log::Level::info,
        value.attached ? "ev=ember_movie result=hud_filter_attached"
                       : "ev=ember_movie result=hud_filter_attach_failed");
}
void uninstall_ember_movie_hud() noexcept {
    static_cast<void>(hooking::detour::uninstall(handle));
}
}
