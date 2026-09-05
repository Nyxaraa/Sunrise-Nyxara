#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../../../state/activity/runtime.h"
#include "../../hooking/detour.h"
#include "internal.h"
#include "spawn/probe.h"

namespace sunrise::client::hooks::bootflow {
namespace {

/**
 * The player spawn gate. Anchored on the load of the encrypted manager global, then run on
 * through the stack-cookie store because the wildcarded frame size leaves the head too short.
 */
constexpr std::string_view kSpawnGateSignatureText =
    "40 53 57 41 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 8B D9 "
    "40 B7 01";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kSpawnGateSignature =
    signature<signature_length(kSpawnGateSignatureText)>(kSpawnGateSignatureText);

/** Answer that holds the spawn for this tick. The gate is polled, so a refusal only delays it. */
constexpr bool kHeld = false;

using SpawnGate = bool(__fastcall*)(std::int32_t) noexcept;

hooking::detour::Handle g_handle{};
std::atomic<SpawnGate> g_original{nullptr};
std::atomic<std::uint64_t> g_lastProbeTick{};
std::atomic<spawn::Refusal> g_lastRefusal{spawn::Refusal::unknown};

/** Reports a changed refusal immediately and a persistent refusal every five seconds. */
void report_spawn_refusal(std::int32_t datum,
                          state::activity::WorldPhase phase,
                          std::uint64_t age) noexcept {
    const spawn::Reading reading = spawn::examine(datum);
    const std::uint64_t now = GetTickCount64();
    const spawn::Refusal previous = g_lastRefusal.exchange(reading.refusal);
    const std::uint64_t last = g_lastProbeTick.load(std::memory_order_relaxed);
    if (reading.refusal == previous && now - last < 5'000U) {
        return;
    }
    g_lastProbeTick.store(now, std::memory_order_relaxed);
    std::array<char, core::log::kLineCapacity> fields{};
    const std::size_t count = spawn::describe(reading, fields);
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=bootflow stage=spawn_gate result=blocked phase=%u age=%llu %.*s",
                                      static_cast<unsigned>(phase),
                                      static_cast<unsigned long long>(age),
                                      static_cast<int>(count),
                                      fields.data());
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

/**
 * Puts the spawn after the world-transition fade is armed.
 * A release on a channel that is not up does nothing, so a spawn during the load leaves the
 * screen black. The client's own predicate reads a host field this destination never fills.
 * @param datum Borrowed player datum handle; the answer does not depend on it.
 * @return The native answer, or held while a destination load is still running.
 */
__declspec(noinline) bool __fastcall spawn_gate(std::int32_t datum) noexcept {
    const SpawnGate original = g_original.load(std::memory_order_acquire);
    const bool allowed = original != nullptr && original(datum);
    observe_world_step();
    const state::activity::WorldPhase phase = state::activity::world_phase();
    const bool transitioning = phase == state::activity::WorldPhase::transitioning;
    // Zero unless a load is running.
    const std::uint64_t age = state::activity::world_transition_age();
    const core::settings::client::Settings& client = core::settings::get().client;
    const bool gaveUp = age >= client.spawnHoldMs;
    const bool loading = transitioning && !gaveUp && client.holdSpawn;
    // Release only on arrival. The step-37 exit re-arms the fade unless one is already up, and
    // nothing polls this gate after the spawn, so an early release leaves a fade nobody clears.
    if (phase == state::activity::WorldPhase::arrived) {
        release_world_fade();
    }
    if (!allowed && transitioning) {
        report_spawn_refusal(datum, phase, age);
    }
    return allowed && loading ? kHeld : allowed;
}

} // namespace

/** Stages the spawn hold. */
StageResult stage_spawn_hold(hooking::detour::Spec& spec) noexcept {
    if (g_handle.attached) {
        return StageResult::attached;
    }
    std::byte* const target = scan_main_image_unique(kSpawnGateSignature, "player_spawn_gate");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=spawn_hold result=fail reason=target");
        return StageResult::unavailable;
    }
    if (!spawn::resolve(target)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=current_slice result=fail reason=targets");
    }
    spec = hooking::detour::Spec{target, reinterpret_cast<void*>(&spawn_gate)};
    return StageResult::staged;
}

/** Takes the spawn hold's attached handle, or a detached one. */
void publish_spawn_hold(const hooking::detour::Handle& handle) noexcept {
    if (!handle.attached) {
        spawn::forget();
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=spawn_hold result=fail reason=attach");
        return;
    }
    g_handle = handle;
    g_original.store(reinterpret_cast<SpawnGate>(g_handle.original), std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=spawn_hold result=ok");
}

/** Detaches the spawn hold. */
void uninstall_spawn_hold() noexcept {
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_original.store(nullptr, std::memory_order_release);
    g_lastProbeTick.store(0, std::memory_order_relaxed);
    g_lastRefusal.store(spawn::Refusal::unknown, std::memory_order_relaxed);
    spawn::forget();
}

} // namespace sunrise::client::hooks::bootflow
