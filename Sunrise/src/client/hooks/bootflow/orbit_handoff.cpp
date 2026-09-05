#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../../hooking/detour.h"
#include "internal.h"

namespace sunrise::client::hooks::bootflow {
namespace {

/**
 * The destination-hold predicate of the orbit setup step. Its prologue repeats across the image,
 * so the pattern runs on through the call and the flag test that follow. Every displacement is
 * wildcarded.
 */
constexpr std::string_view kHoldSignatureText =
    "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 80 3D ? ? ? ? 00 48 8B F8 75 ?";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kHoldSignature = signature<signature_length(kHoldSignatureText)>(kHoldSignatureText);

/** Answer that lets the handoff test pass without waiting. */
constexpr bool kReleased = false;

/** The hold predicate this detour replaces. */
using Hold = bool(__fastcall*)(void*);

hooking::detour::Handle g_handle{};
std::atomic_bool g_reported{false};

/** Writes the one line naming which answer this run uses. */
void report(const char* result) noexcept {
    if (g_reported.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    std::array<char, 96> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=bootflow stage=orbit_handoff result=%s", result);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Answers the destination hold. The game's predicate waits for an armed destination or a
 * starting cinematic, so it owns the answer unless the settings ask to skip that wait.
 * @param stepCtx Borrowed step context, passed through unchanged.
 * @return The game's answer, or the released answer when the skip is on.
 */
__declspec(noinline) bool __fastcall destination_hold(void* stepCtx) noexcept {
    const auto original = reinterpret_cast<Hold>(g_handle.original);
    if (core::settings::get().client.skipOrbitCinematicWait || original == nullptr) {
        report(original == nullptr ? "released_no_trampoline" : "released");
        return kReleased;
    }
    report("native");
    return original(stepCtx);
}

} // namespace

/**
 * Stages the orbit handoff release.
 * @param spec Receives the target and replacement.
 * @return True when the target is found and the fix wants attaching.
 */
StageResult stage_orbit_handoff(hooking::detour::Spec& spec) noexcept {
    if (g_handle.attached) {
        return StageResult::attached;
    }
    std::byte* const target = scan_main_image_unique(kHoldSignature, "orbit_destination_hold");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=orbit_handoff result=fail reason=target");
        return StageResult::unavailable;
    }
    spec = hooking::detour::Spec{target, reinterpret_cast<void*>(&destination_hold)};
    return StageResult::staged;
}

/** Takes the orbit handoff release's attached handle, or a detached one. */
void publish_orbit_handoff(const hooking::detour::Handle& handle) noexcept {
    if (!handle.attached) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=orbit_handoff result=fail reason=attach");
        return;
    }
    g_handle = handle;
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=orbit_handoff result=ok");
}

/** Detaches the orbit handoff release. */
void uninstall_orbit_handoff() noexcept {
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_reported.store(false, std::memory_order_release);
}

} // namespace sunrise::client::hooks::bootflow
