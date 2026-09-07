#include "async_io_lifetime_guard.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../patterns/image_scan.h"
#include "../../process/freeze/client_process_freeze.h"

namespace sunrise::client::hooks::async_io {
namespace {

using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/**
 * Post-pump tail of the native async-I/O manager wrapper. The RIP-relative and call displacements
 * are ASLR-stable bytes in this build and separate this wrapper from two identical manager tails.
 * A different executable fails closed rather than accepting a structurally similar owner.
 */
constexpr std::string_view kTailText =
    "48 8B 1D E6 3C 30 02 48 8B CB E8 66 89 00 00 48 8B 05 D7 3C 30 02 "
    "80 B8 62 6C 04 00 00 74 15 "
    "8B 08 33 D2 C6 80 62 6C 04 00 00";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kTail = signature<signature_length(kTailText)>(kTailText);

/** The post-pump global reload begins after MOV RBX, MOV RCX, and CALL. */
constexpr std::size_t kReloadOffset = 15;
/** `mov rax, rbx` followed by one four-byte architectural NOP. */
constexpr std::array<std::byte, 7> kCachedOwner{std::byte{0x48},
                                                std::byte{0x89},
                                                std::byte{0xD8},
                                                std::byte{0x0F},
                                                std::byte{0x1F},
                                                std::byte{0x40},
                                                std::byte{0x00}};
/** `mov rax, [rip+disp32]`, the first three bytes of the untouched native reload. */
constexpr std::array<std::byte, 3> kNativeReloadPrefix{
    std::byte{0x48}, std::byte{0x8B}, std::byte{0x05}};
/** Freeze retries before the patch is abandoned; a thread inside the bytes blocks the write. */
constexpr std::size_t kHoldAttempts = 8;

std::byte* g_reload{};
std::array<std::byte, kCachedOwner.size()> g_original{};
bool g_installed{};
bool g_ownsPatch{};

/** @return True when a suspended thread would resume inside the bytes being replaced. */
[[nodiscard]] bool thread_inside_patch(const process::freeze::Held& held,
                                       const std::byte* start) noexcept {
    const auto low = reinterpret_cast<std::uintptr_t>(start);
    const auto high = low + kCachedOwner.size();
    for (std::size_t index = 0; index < held.count; ++index) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(held.handles[index], &context) == FALSE) {
            return true;
        }
        const auto instruction = static_cast<std::uintptr_t>(context.Rip);
        if (instruction >= low && instruction < high) {
            return true;
        }
    }
    return false;
}

/** Suspends the process only when no native thread stopped in the patch window. */
[[nodiscard]] bool hold_away_from_patch(process::freeze::Held& held,
                                        const std::byte* start) noexcept {
    for (std::size_t attempt = 0; attempt < kHoldAttempts; ++attempt) {
        if (!process::freeze::hold(held)) {
            return false;
        }
        if (!thread_inside_patch(held, start)) {
            return true;
        }
        process::freeze::release(held);
        Sleep(1);
    }
    return false;
}

/** Writes one complete instruction replacement while every competing process thread is held. */
template <std::size_t Size>
[[nodiscard]] bool write_code(std::byte* destination,
                              const std::array<std::byte, Size>& bytes) noexcept {
    process::freeze::Held held{};
    if (destination == nullptr || !hold_away_from_patch(held, destination)) {
        return false;
    }
    DWORD originalProtection = 0;
    bool written =
        VirtualProtect(destination, bytes.size(), PAGE_EXECUTE_READWRITE, &originalProtection)
        != FALSE;
    if (written) {
        std::memcpy(destination, bytes.data(), bytes.size());
        written = FlushInstructionCache(GetCurrentProcess(), destination, bytes.size()) != FALSE;
        DWORD ignored = 0;
        written = VirtualProtect(destination, bytes.size(), originalProtection, &ignored) != FALSE
                  && written;
    }
    process::freeze::release(held);
    return written;
}

} // namespace

/** Installs the native async-manager cached-owner guard when this game build matches. */
bool install() noexcept {
    if (g_installed) {
        return true;
    }
    std::byte* const tail = scan_main_image_unique(kTail, "async_io_manager_post_pump_tail");
    if (tail == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=async_io_guard stage=resolve result=fail");
        return false;
    }
    g_reload = tail + kReloadOffset;
    if (std::memcmp(g_reload, kCachedOwner.data(), kCachedOwner.size()) == 0) {
        g_installed = true;
        g_ownsPatch = false;
        return true;
    }
    if (std::memcmp(g_reload, kNativeReloadPrefix.data(), kNativeReloadPrefix.size()) != 0) {
        g_reload = nullptr;
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=async_io_guard stage=verify result=fail");
        return false;
    }
    std::memcpy(g_original.data(), g_reload, g_original.size());
    const bool patchWriteComplete = write_code(g_reload, kCachedOwner);
    if (!patchWriteComplete
        && std::memcmp(g_reload, kCachedOwner.data(), kCachedOwner.size()) != 0) {
        g_reload = nullptr;
        g_original = {};
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=async_io_guard stage=patch result=fail");
        return false;
    }
    g_installed = true;
    g_ownsPatch = true;
    core::log::write(core::log::Channel::client,
                     patchWriteComplete ? core::log::Level::info : core::log::Level::warn,
                     "ev=async_io_guard stage=patch result=ok mode=cached_manager_owner");
    return true;
}

/** Restores the exact original instruction bytes when this module installed the guard. */
bool uninstall() noexcept {
    if (!g_installed) {
        return true;
    }
    if (g_ownsPatch) {
        const bool restoreComplete = write_code(g_reload, g_original);
        if (!restoreComplete && std::memcmp(g_reload, g_original.data(), g_original.size()) != 0) {
            return false;
        }
    }
    g_reload = nullptr;
    g_original = {};
    g_installed = false;
    g_ownsPatch = false;
    return true;
}

/** @return True while the guarded instruction sequence is present. */
bool is_installed() noexcept {
    return g_installed;
}

} // namespace sunrise::client::hooks::async_io
