#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../../hooking/detour.h"
#include "internal.h"

namespace sunrise::client::hooks::bootflow {
namespace {

/**
 * The profile-setup step's update. Anchored on the screen-settled call, its taken branch and the
 * read of the step's own state field. Together they are unique to it. Every displacement is
 * wildcarded.
 */
constexpr std::string_view kUpdateSignatureText =
    "40 53 48 83 EC ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B D9 E8 ? ? ? ? 84 C0 0F 84 ? "
    "? ? ? 8B 43 38 85 C0 75 ?";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kUpdateSignature =
    signature<signature_length(kUpdateSignatureText)>(kUpdateSignatureText);

/** Fields of the boot step this hook touches, as byte offsets from the step's own base. */
struct StepLayout {
    /** The step's state, which its update switches on. */
    static constexpr std::size_t state = 56;
};

/**
 * States with no self-advance. The update posts a setup screen and parks in one of these until
 * the UI raises the investment event that acknowledges it. State 0 is the entry that posts the
 * first one.
 */
constexpr std::array<std::uint32_t, 5> kWaitingStates{0, 1, 3, 5, 7};

/**
 * State that matches no case in the update's dispatch, so it falls to the last block. That block
 * marks the profile record and hands off to the next step. It is the same exit the stock game
 * takes once every setup screen is acknowledged.
 */
constexpr std::uint32_t kHandoffState = 9;

/** The update's not-complete answer, returned when the trampoline is gone. */
constexpr char kStepIncomplete = 0;

using Update = char(__fastcall*)(std::byte*);

hooking::detour::Handle g_handle{};
std::atomic<Update> g_original{nullptr};
std::atomic_bool g_reported{false};

/** @return True when the step cannot advance on its own. */
[[nodiscard]] bool is_waiting(std::uint32_t state) noexcept {
    for (const std::uint32_t waiting : kWaitingStates) {
        if (state == waiting) {
            return true;
        }
    }
    return false;
}

/**
 * Skips the startup setup screens. A UI event this setup never raises acknowledges them, so the
 * step would park forever. Moving it to the handoff state before the update runs takes the step's
 * own exit without posting a screen.
 * @param step Borrowed boot-step base pointer.
 * @return The update's own result.
 */
__declspec(noinline) char __fastcall update(std::byte* step) noexcept {
    const Update original = g_original.load(std::memory_order_acquire);
    if (!core::settings::get().client.skipProfileSetup) {
        if (!g_reported.exchange(true, std::memory_order_relaxed)) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             "ev=bootflow stage=profile_setup result=disabled");
        }
    } else if (step != nullptr) {
        std::uint32_t state = 0;
        std::memcpy(&state, step + StepLayout::state, sizeof state);
        if (is_waiting(state)) {
            std::memcpy(step + StepLayout::state, &kHandoffState, sizeof kHandoffState);
            if (!g_reported.exchange(true, std::memory_order_relaxed)) {
                core::log::write(core::log::Channel::client,
                                 core::log::Level::info,
                                 "ev=bootflow stage=profile_setup result=skipped");
            }
        }
    }
    return original != nullptr ? original(step) : kStepIncomplete;
}

} // namespace

/**
 * Stages the profile-setup skip.
 * @param spec Receives the target and replacement.
 * @return staged when the target is found, unavailable on a miss.
 */
StageResult stage_profile_setup_skip(hooking::detour::Spec& spec) noexcept {
    if (g_handle.attached) {
        return StageResult::attached;
    }
    std::byte* const target = scan_main_image_unique(kUpdateSignature, "profile_setup_update");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=profile_setup result=fail reason=target");
        return StageResult::unavailable;
    }
    spec = hooking::detour::Spec{target, reinterpret_cast<void*>(&update)};
    return StageResult::staged;
}

/** Takes the profile-setup skip's attached handle, or a detached one. */
void publish_profile_setup_skip(const hooking::detour::Handle& handle) noexcept {
    if (!handle.attached) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=profile_setup result=fail reason=attach");
        return;
    }
    g_handle = handle;
    g_original.store(reinterpret_cast<Update>(g_handle.original), std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=profile_setup result=ok");
}

/** Detaches the profile-setup skip. */
void uninstall_profile_setup_skip() noexcept {
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_original.store(nullptr, std::memory_order_release);
    g_reported.store(false, std::memory_order_release);
}

} // namespace sunrise::client::hooks::bootflow
