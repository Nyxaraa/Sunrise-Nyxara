#include "ui_mask.h"
#include "../../../core/logging/log.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"
#include <cstring>

namespace sunrise::client::native::presentation {
namespace {
using namespace patterns;
using Getter = void*(__fastcall*)();
using Fade = void(__fastcall*)(int);
Getter windows{}, uiSnapshot{};
using StateReader = int(__fastcall*)(void*);
using WindowSelector = int(__fastcall*)(int);
StateReader uiState{};
WindowSelector selectedWindow{};
using SelectState = void(__fastcall*)(int);
SelectState selectState{};
int previousMovieState{-1};
bool movieStateOwned{};
Fade fadeIn{}, fadeOut{};
bool attempted{}, curtainOwned{};
template <class T> T field(void* pointer, unsigned offset) {
    T value{};
    std::memcpy(&value, static_cast<std::byte*>(pointer) + offset, sizeof(value));
    return value;
}
void* target(std::byte* code, unsigned offset) {
    if (!code || code[offset] != std::byte{0xE8}) return nullptr;
    return code + offset + 5 + field<std::int32_t>(code, offset + 1);
}
bool resolve() {
    if (attempted) return windows && uiSnapshot && uiState && selectedWindow && selectState && fadeIn && fadeOut;
    attempted = true;
    constexpr auto managerSig = signature<signature_length(
        "41 89 5D 0C E8 ? ? ? ? 4C 8B F8 E8 ? ? ? ? 89 45 0F E8 ? ? ? ? 48 8B C8")>(
        "41 89 5D 0C E8 ? ? ? ? 4C 8B F8 E8 ? ? ? ? 89 45 0F E8 ? ? ? ? 48 8B C8");
    constexpr auto stateSig = signature<signature_length(
        "40 53 48 83 EC 20 8B D9 E8 ? ? ? ? 84 C0 75 14 E8 ? ? ? ? 48 8B C8 8B D3 48 83 C4 20 5B E9 DB E3 FE FF")>(
        "40 53 48 83 EC 20 8B D9 E8 ? ? ? ? 84 C0 75 14 E8 ? ? ? ? 48 8B C8 8B D3 48 83 C4 20 5B E9 DB E3 FE FF");
    constexpr auto fadeInSig = signature<signature_length(
        "48 83 EC 28 80 3D ? ? ? ? 00 74 ? 48 89 5C 24 20 48 8D 1D ? ? ? ? 48 83 E3 F0 F6 83 98 01 00 00 01")>(
        "48 83 EC 28 80 3D ? ? ? ? 00 74 ? 48 89 5C 24 20 48 8D 1D ? ? ? ? 48 83 E3 F0 F6 83 98 01 00 00 01");
    constexpr auto fadeOutSig = signature<signature_length(
        "80 3D ? ? ? ? 00 8B D1 74 ? 48 8D 0D ? ? ? ? 48 83 E1 F0 F6 81 98 01 00 00 01")>(
        "80 3D ? ? ? ? 00 8B D1 74 ? 48 8D 0D ? ? ? ? 48 83 E1 F0 F6 81 98 01 00 00 01");
    auto* manager = scan_main_image_unique(managerSig, "presentation_windows");
    windows = reinterpret_cast<Getter>(target(manager, 4));
    uiSnapshot = reinterpret_cast<Getter>(target(manager, 0x14));
    uiState = reinterpret_cast<StateReader>(target(manager, 0x1C));
    selectedWindow = reinterpret_cast<WindowSelector>(target(manager, 0x29));
    selectState = reinterpret_cast<SelectState>(scan_main_image_unique(stateSig, "presentation_movie_state"));
    fadeIn = reinterpret_cast<Fade>(scan_main_image_unique(fadeInSig, "presentation_fade_in"));
    fadeOut = reinterpret_cast<Fade>(scan_main_image_unique(fadeOutSig, "presentation_fade_out"));
    return windows && uiSnapshot && uiState && selectedWindow && selectState && fadeIn && fadeOut;
}
}

bool set_movie_presentation(bool enabled) {
    if (!enabled && !movieStateOwned) return true;
    if (!resolve()) return false;
    auto* snapshot = uiSnapshot();
    if (!snapshot) return false;
    const int current = uiState(snapshot);
    if (!enabled) {
        if (current == 0x24 && previousMovieState >= 0)
            selectState(previousMovieState);
        movieStateOwned = false;
        previousMovieState = -1;
        return true;
    }
    // Preserve error, loading and menu transitions. Only replace gameplay presentation.
    const int window = selectedWindow(current);
    if (window != 0x1B && current != 0x24) return true;
    if (!movieStateOwned) {
        if (current == 0x24) return true;
        previousMovieState = current;
        movieStateOwned = true;
    }
    if (current != 0x24) selectState(0x24);
    return true;
}

bool mask_loading_flight(bool enabled) {
    if (!enabled && !curtainOwned) return true;
    if (!resolve()) return false;
    auto* manager = windows();
    if (!manager) return false;
    if (!enabled) {
        // Release only a curtain this request raised; do not cancel an existing native fade.
        fadeOut(0);
        curtainOwned = false;
        core::log::write(core::log::Channel::client, core::log::Level::info,
                         "ev=loading_mask result=released");
    } else if (field<unsigned char>(manager, 0x940)
               && !(field<unsigned char>(manager, 0x198) & 1)) {
        fadeIn(0);
        if (!curtainOwned)
            core::log::write(core::log::Channel::client, core::log::Level::info,
                             "ev=loading_mask result=raised");
        curtainOwned = true;
    }
    return true;
}
bool loading_screen_selected() {
    if (!resolve() || !uiSnapshot || !uiState || !selectedWindow) return false;
    auto* snapshot = uiSnapshot();
    return snapshot && selectedWindow(uiState(snapshot)) == 0x1D;
}
}
