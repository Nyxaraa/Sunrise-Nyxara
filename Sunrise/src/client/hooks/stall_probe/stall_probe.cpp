/**
 * Names the code a silent freeze blocks on.
 * Some freezes stop the game's own watchdog with the rest of the game, so no hitch snapshot
 * ever names the blocked code. This watcher polls the pump call count from its own thread;
 * when the count stops moving it captures every thread's rip and in-image return addresses.
 */

#include "stall_probe.h"

#include <Windows.h>

#include <TlHelp32.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../../core/logging/log.h"
#include "../../../steam/runtime/runtime.h"
#include "../../diagnostics/module_range.h"
#include "../../memory/current_process_memory.h"
#include "../../process/freeze/client_process_freeze.h"

namespace sunrise::client::hooks::stall_probe {
namespace {

/** Poll cadence, and how long the count must sit still before a dump. */
constexpr DWORD kPollMs = 500;
constexpr std::uint64_t kStallMs = 6'000;
/** Repeat dumps of one long stall, spaced and capped: rip movement separates spin from wait. */
constexpr std::uint64_t kRedumpMs = 15'000;
constexpr unsigned kMaxDumps = 8;
/** Bytes of stack copied per thread, halved until a read succeeds. */
constexpr std::size_t kStackWindowBytes = 2048;
/** Module-relative return addresses kept from that window, and how many share one line. */
constexpr std::size_t kFrameLimit = 16;
constexpr std::size_t kFramesPerLine = 8;
/** Time the stopping side waits for the watcher to leave its loop. */
constexpr DWORD kJoinMs = 2'000;

HANDLE g_thread{};
HANDLE g_stop{};
std::atomic_bool g_installed{false};
diagnostics::ModuleRange g_gameRange{};
diagnostics::ModuleRange g_ownRange{};

/** Resolves the game image range and this DLL's own range once. */
void resolve_module_ranges() noexcept {
    (void)diagnostics::module_range(GetModuleHandleW(nullptr), g_gameRange);
    HMODULE own = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&resolve_module_ranges),
                       &own);
    (void)diagnostics::module_range(own, g_ownRange);
}

/** Formats one code address as a module-relative token, or raw hex outside both images. */
void format_address(std::uint64_t value, char* out, std::size_t size) noexcept {
    if (diagnostics::contains(g_gameRange, value)) {
        std::snprintf(
            out, size, "exe+0x%llX", static_cast<unsigned long long>(value - g_gameRange.base));
        return;
    }
    if (diagnostics::contains(g_ownRange, value)) {
        std::snprintf(
            out, size, "own+0x%llX", static_cast<unsigned long long>(value - g_ownRange.base));
        return;
    }
    std::snprintf(out, size, "0x%llX", static_cast<unsigned long long>(value));
}

/**
 * Suspends one thread, copies its control registers and a stack window, then resumes it.
 * Only memory is taken while the thread is suspended; all logging happens after the resume.
 * @return True when the context was captured. A short or absent stack window is still success.
 */
[[nodiscard]] bool capture_thread(std::uint32_t tid,
                                  CONTEXT& context,
                                  std::byte* stack,
                                  std::size_t& stackBytes) noexcept {
    stackBytes = 0;
    const HANDLE thread = OpenThread(
        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
    if (thread == nullptr) {
        return false;
    }
    bool captured = false;
    if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
        context.ContextFlags = CONTEXT_CONTROL;
        captured = GetThreadContext(thread, &context) != 0;
        if (captured) {
            for (std::size_t size = kStackWindowBytes; size >= 256; size /= 2) {
                if (memory::read_current_process(nullptr, context.Rsp, std::span(stack, size))) {
                    stackBytes = size;
                    break;
                }
            }
        }
        ResumeThread(thread);
    }
    CloseHandle(thread);
    return captured;
}

/** Logs one thread's rip, rsp and every in-image return address on its stack. */
void report_thread(std::uint32_t tid) noexcept {
    alignas(16) CONTEXT context{};
    std::array<std::byte, kStackWindowBytes> stack{};
    std::size_t stackBytes = 0;
    if (!capture_thread(tid, context, stack.data(), stackBytes)) {
        return;
    }
    std::array<char, 32> ripText{};
    format_address(context.Rip, ripText.data(), ripText.size());
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=probe stage=stall set=stack tid=0x%08X rip=%s rsp=0x%llX "
                                "window=%zu",
                                tid,
                                ripText.data(),
                                static_cast<unsigned long long>(context.Rsp),
                                stackBytes);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    std::size_t frames = 0;
    std::size_t onLine = 0;
    std::size_t offset = 0;
    for (std::size_t index = 0; index * 8 + 8 <= stackBytes && frames < kFrameLimit; ++index) {
        std::uint64_t value = 0;
        std::memcpy(&value, stack.data() + index * 8, sizeof value);
        if (!diagnostics::contains(g_gameRange, value)
            && !diagnostics::contains(g_ownRange, value)) {
            continue;
        }
        if (onLine == 0) {
            const int prefix = std::snprintf(
                line.data(), line.size(), "ev=probe stage=stall set=frames tid=0x%08X", tid);
            offset = prefix > 0 ? static_cast<std::size_t>(prefix) : 0;
        }
        std::array<char, 32> text{};
        format_address(value, text.data(), text.size());
        const int piece = std::snprintf(
            line.data() + offset, line.size() - offset, " f%zu=%s", frames, text.data());
        if (piece > 0) {
            offset += static_cast<std::size_t>(piece);
        }
        ++frames;
        ++onLine;
        if (onLine == kFramesPerLine || frames == kFrameLimit) {
            core::log::write(
                core::log::Channel::client, core::log::Level::info, {line.data(), offset});
            onLine = 0;
        }
    }
    if (onLine != 0) {
        core::log::write(core::log::Channel::client, core::log::Level::info, {line.data(), offset});
    }
}

/** Captures every other thread in the process, one at a time, as the only suspender. */
void dump_all_threads() noexcept {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    // A Detours transaction suspends threads too; two suspenders at once freeze each other.
    process::freeze::enter_exclusive();
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    const DWORD processId = GetCurrentProcessId();
    const DWORD currentThreadId = GetCurrentThreadId();
    for (BOOL available = Thread32First(snapshot, &entry); available != FALSE;
         available = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != currentThreadId) {
            report_thread(entry.th32ThreadID);
        }
    }
    process::freeze::leave_exclusive();
    CloseHandle(snapshot);
}

/** Writes the one-line stall marker. @param result "detected" or "recovered". */
void report_marker(const char* result, std::uint64_t idleMs, std::uint64_t count) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=probe stage=stall result=%s idle=%llu count=%llu",
                                      result,
                                      static_cast<unsigned long long>(idleMs),
                                      static_cast<unsigned long long>(count));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** The watcher loop: poll the pump count, dump when it sits still. */
DWORD WINAPI watch(LPVOID) noexcept {
    std::uint64_t lastCount = steam::callback_count();
    std::uint64_t lastMoveMs = GetTickCount64();
    std::uint64_t nextDumpMs = 0;
    unsigned dumps = 0;
    bool stalled = false;
    while (WaitForSingleObject(g_stop, kPollMs) == WAIT_TIMEOUT) {
        const std::uint64_t count = steam::callback_count();
        const std::uint64_t now = GetTickCount64();
        if (count != lastCount) {
            if (stalled) {
                report_marker("recovered", now - lastMoveMs, count);
                stalled = false;
            }
            lastCount = count;
            lastMoveMs = now;
            continue;
        }
        // The count only moves once the game starts pumping, so boot does not read as a stall.
        if (count == 0 || now - lastMoveMs < kStallMs || now < nextDumpMs || dumps >= kMaxDumps) {
            continue;
        }
        report_marker("detected", now - lastMoveMs, count);
        stalled = true;
        dump_all_threads();
        ++dumps;
        nextDumpMs = now + kRedumpMs;
    }
    return 0;
}

} // namespace

/** Starts the stall watcher thread. */
bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    resolve_module_ranges();
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stop == nullptr) {
        return false;
    }
    g_thread = CreateThread(nullptr, 0, &watch, nullptr, 0, nullptr);
    if (g_thread == nullptr) {
        CloseHandle(g_stop);
        g_stop = nullptr;
        return false;
    }
    g_installed.store(true, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=probe stage=stall result=installed");
    return true;
}

/** Stops the watcher thread. */
bool uninstall() noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    SetEvent(g_stop);
    (void)WaitForSingleObject(g_thread, kJoinMs);
    CloseHandle(g_thread);
    CloseHandle(g_stop);
    g_thread = nullptr;
    g_stop = nullptr;
    g_installed.store(false, std::memory_order_release);
    return true;
}

} // namespace sunrise::client::hooks::stall_probe
