#include "net_tick_probe.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "../../../core/logging/log.h"
#include "../../../steam/runtime/runtime.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"

namespace sunrise::client::hooks::net_tick_probe {
namespace {

/**
 * The guarded entry of the game's whole networking tick.
 * It reads an enable byte, then calls the re-entrancy test and returns when that test is true.
 * Both RIP-relative operands, the call and both jump displacements are wildcarded.
 */
inline constexpr std::string_view kTickGuardText =
    "48 83 EC 28 80 3D ? ? ? ? 00 0F 84 ? ? ? ? E8 ? ? ? ? 84 C0 0F 85";
/** Compiled length of the tick-guard signature, counted from its text at build time. */
inline constexpr std::size_t kTickGuardSize = patterns::signature_length(kTickGuardText);
constinit const std::array<patterns::PatternByte, kTickGuardSize> kTickGuard =
    patterns::signature<kTickGuardSize>(kTickGuardText);

/** Displacement of the call to the re-entrancy test, and the instruction after it. */
constexpr std::size_t kTestCallOperand = 18;
constexpr std::size_t kTestCallNext = 22;

/** The test is `movzx eax, byte ptr [rip+disp]` then `retn`, so its shape is checkable. */
constexpr std::byte kMovzxEaxByte0{0x0F};
constexpr std::byte kMovzxEaxByte1{0xB6};
constexpr std::byte kMovzxEaxByte2{0x05};
constexpr std::byte kRetn{0xC3};
/** Displacement of the latch inside the test, and the instruction after it. */
constexpr std::size_t kLatchOperand = 3;
constexpr std::size_t kLatchNext = 7;
/** Byte after the whole test, which must be the return. */
constexpr std::size_t kTestReturn = 7;

/** One line per this many milliseconds, so a stalled tick reports without flooding. */
constexpr std::uint64_t kSampleIntervalMs = 2'000;

std::atomic<bool> g_resolved{};
std::atomic<const std::uint8_t*> g_latch{};
std::atomic<std::uint64_t> g_dueTick{};

/** Resolves the latch address from two decoded operands. Runs at most once. */
void resolve_once() noexcept {
    if (g_resolved.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    std::byte* const guard = patterns::scan_main_image_unique(kTickGuard, "net_tick_guard");
    if (guard == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=nettick stage=resolve result=fail reason=signature");
        return;
    }
    const std::byte* const test =
        patterns::resolve_relative(guard + kTestCallOperand, guard + kTestCallNext);
    // The second derivation reads an operand out of whatever the call landed on, so the shape of
    // that function is checked before its bytes are trusted as an operand.
    if (test == nullptr || test[0] != kMovzxEaxByte0 || test[1] != kMovzxEaxByte1
        || test[2] != kMovzxEaxByte2 || test[kTestReturn] != kRetn) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=nettick stage=resolve result=fail reason=shape");
        return;
    }
    const auto* const latch = reinterpret_cast<const std::uint8_t*>(
        patterns::resolve_relative(test + kLatchOperand, test + kLatchNext));
    if (latch == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=nettick stage=resolve result=fail reason=operand");
        return;
    }
    g_latch.store(latch, std::memory_order_release);
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=nettick stage=resolve result=ok latch=0x%p",
                                      static_cast<const void*>(latch));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

/** Reports the game's networking-tick latch and our own callback count. */
void sample() noexcept {
    resolve_once();
    const std::uint8_t* const latch = g_latch.load(std::memory_order_acquire);
    if (latch == nullptr) {
        return;
    }
    const std::uint64_t now = GetTickCount64();
    std::uint64_t due = g_dueTick.load(std::memory_order_relaxed);
    if (now < due
        || !g_dueTick.compare_exchange_strong(
            due, now + kSampleIntervalMs, std::memory_order_relaxed)) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=nettick stage=sample latch=%u callbacks=%llu",
                                      static_cast<unsigned>(*latch),
                                      static_cast<unsigned long long>(steam::callback_count()));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace sunrise::client::hooks::net_tick_probe
