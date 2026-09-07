/**
 * Read-only dump of the hitch watchdog's job snapshot.
 * The watchdog line names one stalled fiber-phase job and nothing else. The snapshot it is
 * printed from carries every in-flight job record, with the blocked thread id. This probe logs
 * them all at the first hitch, which names the job the in-world freeze blocks on.
 */

#include "hitch_probe.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "../../memory/current_process_memory.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"

namespace sunrise::client::hooks::hitch_probe {
namespace {

using patterns::resolve_relative;
using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/** The snapshot fill the watchdog report builds its line from. This is the detour target. */
constexpr std::string_view kFillText =
    "89 54 24 10 48 89 4C 24 08 48 83 EC 78 33 C0 85 C0 75 FA 48 83 BC 24 80 00 00 00 00 0F 84 "
    "? ? ? ? 48 8B 8C 24 80 00 00 00 E8";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kFill = signature<signature_length(kFillText)>(kFillText);

/** The stalled-job format site. Its two calls are the type lookup and the description printer. */
constexpr std::string_view kFormatText =
    "48 89 84 24 80 01 00 00 48 8B 84 24 80 01 00 00 8B 40 68 89 44 24 38 48 8B 84 24 00 03 00 "
    "00 48 8B 80 20 6A 00 00 48 05 80 00 00 00 48 89 84 24 88 01 00 00 48 8D 4C 24 38 E8 ? ? ? "
    "? 4C 8D 84 24 E0 01 00 00 48 8B 8C 24 88 01 00 00 48 8B D1 48 8B C8 E8 ? ? ? ?";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kFormat = signature<signature_length(kFormatText)>(kFormatText);

/** Description-type lookup call operand inside the format site, and the next instruction. */
constexpr std::size_t kLookupOperand = 58;
constexpr std::size_t kLookupNext = 62;
/** Description printer call operand inside the format site, and the next instruction. */
constexpr std::size_t kPrintOperand = 85;
constexpr std::size_t kPrintNext = 89;

/**
 * The net-tick gate poll: one gate load, then one idle test per frame fiber.
 * The three fiber-handle loads and the idle-test call are decoded from its operands.
 */
constexpr std::string_view kGatePollText =
    "8B 05 ? ? ? ? 85 C0 75 ? 8B 0D ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 8B 0D ? ? ? ? E8 ? ? ? ? 84 "
    "C0 74 ? 8B 0D ? ? ? ? E8 ? ? ? ? 84 C0 74 ? B8 01 00 00 00 87 05 ? ? ? ?";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kGatePoll = signature<signature_length(kGatePollText)>(kGatePollText);

/** Operand offsets inside the gate poll: gate, fiber handles, idle test, in match order. */
constexpr std::size_t kGateOperand = 2;
constexpr std::size_t kGateNext = 6;
constexpr std::size_t kFiberAOperand = 12;
constexpr std::size_t kFiberANext = 16;
constexpr std::size_t kIdleOperand = 17;
constexpr std::size_t kIdleNext = 21;
constexpr std::size_t kFiberBOperand = 27;
constexpr std::size_t kFiberBNext = 31;
constexpr std::size_t kFiberCOperand = 42;
constexpr std::size_t kFiberCNext = 46;

/** Snapshot fields: duration, bootflow step, then two arrays of 16 384-byte job records. */
constexpr std::size_t kDurationOffset = 16;
constexpr std::size_t kStepOffset = 24;
constexpr std::size_t kWaitEntriesOffset = 4224;
constexpr std::size_t kRingEntriesOffset = 10880;
constexpr std::size_t kEntryCount = 16;
constexpr std::size_t kEntryStride = 384;

/** Job record fields: age, description copy, kind byte, thread id, wait-slot index. */
constexpr std::size_t kEntryAgeOffset = 8;
constexpr std::size_t kEntryDescOffset = 128;
constexpr std::size_t kEntryKindOffset = 256;
constexpr std::size_t kEntryThreadOffset = 260;
constexpr std::size_t kEntrySlotOffset = 264;
/** Description-type id inside the copied description. Zero marks an unused record. */
constexpr std::size_t kDescTypeOffset = 104;

/** The game prints a description into a 256-byte buffer; this matches it. */
constexpr std::size_t kDescTextCapacity = 256;
/** One dump per interval, capped per process, so a long freeze cannot flood the log. */
constexpr std::uint64_t kReportIntervalMs = 10'000;
constexpr unsigned kMaxReports = 32;
/** Bytes of the suspected thread's stack copied per dump, halved until a read succeeds. */
constexpr std::size_t kStackWindowBytes = 2048;
/** Module-relative return addresses kept from that window, and how many share one line. */
constexpr std::size_t kFrameLimit = 16;
constexpr std::size_t kFramesPerLine = 8;

using Fill = std::int64_t(__fastcall*)(std::byte*, std::uint64_t, std::uint64_t, std::uint64_t);
using DescLookup = const std::byte*(__fastcall*)(const std::uint32_t*);
using DescPrint = std::int64_t(__fastcall*)(const std::byte*, const std::byte*, char*);
using FiberIdle = bool(__fastcall*)(std::uint32_t);

hooking::detour::Handle g_handle{};
std::atomic_bool g_installed{false};
DescLookup g_lookup{};
DescPrint g_print{};
FiberIdle g_fiberIdle{};
const std::uint32_t* g_gate{};
const std::uint32_t* g_fiberA{};
const std::uint32_t* g_fiberB{};
const std::uint32_t* g_fiberC{};
std::atomic<std::uint64_t> g_nextReportTick{0};
std::atomic<unsigned> g_reports{0};

/** Prints one record's description with the game's own printer. POD frame only, for __try. */
[[nodiscard]] bool describe(const std::byte* desc, char* text) noexcept {
    __try {
        const std::uint32_t typeId =
            *reinterpret_cast<const std::uint32_t*>(desc + kDescTypeOffset);
        if (typeId == 0 || g_lookup == nullptr || g_print == nullptr) {
            return false;
        }
        const std::byte* const record = g_lookup(&typeId);
        if (record == nullptr) {
            return false;
        }
        text[0] = 0;
        g_print(record, desc, text);
        text[kDescTextCapacity - 1] = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

/** @return 1 idle, 0 busy, -1 unreadable. POD frame only, for __try. */
[[nodiscard]] int fiber_idle(const std::uint32_t* handle) noexcept {
    __try {
        if (handle == nullptr || g_fiberIdle == nullptr) {
            return -1;
        }
        return g_fiberIdle(*handle) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

/** One loaded image's address range. */
struct ModuleRange {
    std::uintptr_t base{};
    std::uintptr_t size{};
};

ModuleRange g_gameRange{};
ModuleRange g_ownRange{};

/** @param module One loaded module. @return Its image range, from its own PE header. */
[[nodiscard]] ModuleRange module_range(HMODULE module) noexcept {
    if (module == nullptr) {
        return {};
    }
    const auto* header = static_cast<const std::byte*>(static_cast<const void*>(module));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(header);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(header + dos->e_lfanew);
    return {reinterpret_cast<std::uintptr_t>(module), nt->OptionalHeader.SizeOfImage};
}

/** Resolves the game image range and this DLL's own range once. */
void resolve_module_ranges() noexcept {
    g_gameRange = module_range(GetModuleHandleW(nullptr));
    HMODULE own = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&resolve_module_ranges),
                       &own);
    g_ownRange = module_range(own);
}

/** @return True when the value lies inside the given image. */
[[nodiscard]] bool inside(const ModuleRange& range, std::uint64_t value) noexcept {
    return range.base != 0 && value >= range.base && value < range.base + range.size;
}

/** Formats one code address as a module-relative token, or raw hex outside both images. */
void format_address(std::uint64_t value, char* out, std::size_t size) noexcept {
    if (inside(g_gameRange, value)) {
        std::snprintf(
            out, size, "exe+0x%llX", static_cast<unsigned long long>(value - g_gameRange.base));
        return;
    }
    if (inside(g_ownRange, value)) {
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

/** Logs the suspected thread's rip, rsp and every in-image return address on its stack. */
void report_stack(std::uint32_t tid) noexcept {
    if (tid == GetCurrentThreadId()) {
        // The wait record can name the reporting thread itself; suspending it would never return.
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=probe stage=hitch set=stack self=1");
        return;
    }
    alignas(16) CONTEXT context{};
    std::array<std::byte, kStackWindowBytes> stack{};
    std::size_t stackBytes = 0;
    if (!capture_thread(tid, context, stack.data(), stackBytes)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=probe stage=hitch set=stack result=fail");
        return;
    }
    std::array<char, 32> ripText{};
    format_address(context.Rip, ripText.data(), ripText.size());
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=probe stage=hitch set=stack tid=0x%08X rip=%s rsp=0x%llX "
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
        if (!inside(g_gameRange, value) && !inside(g_ownRange, value)) {
            continue;
        }
        if (onLine == 0) {
            const int prefix = std::snprintf(
                line.data(), line.size(), "ev=probe stage=hitch set=frames tid=0x%08X", tid);
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

/** Logs the gate value and each frame fiber's handle and idle state. */
void report_fibers(const std::byte* snapshot) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=probe stage=hitch step=%d dur=%llu gate=%u fa=0x%08X fa_idle=%d fb=0x%08X fb_idle=%d "
        "fc=0x%08X fc_idle=%d",
        *reinterpret_cast<const std::int32_t*>(snapshot + kStepOffset),
        static_cast<unsigned long long>(
            *reinterpret_cast<const std::uint64_t*>(snapshot + kDurationOffset)),
        g_gate != nullptr ? *g_gate : 0U,
        g_fiberA != nullptr ? *g_fiberA : 0U,
        fiber_idle(g_fiberA),
        g_fiberB != nullptr ? *g_fiberB : 0U,
        fiber_idle(g_fiberB),
        g_fiberC != nullptr ? *g_fiberC : 0U,
        fiber_idle(g_fiberC));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** Logs every used record of one snapshot array. @param set Short array name for the line. */
void report_entries(const std::byte* snapshot, std::size_t arrayOffset, const char* set) noexcept {
    for (std::size_t index = 0; index < kEntryCount; ++index) {
        const std::byte* const entry = snapshot + arrayOffset + index * kEntryStride;
        std::array<char, kDescTextCapacity> text{};
        if (!describe(entry + kEntryDescOffset, text.data())) {
            continue;
        }
        std::array<char, core::log::kLineCapacity> line{};
        const int written = std::snprintf(
            line.data(),
            line.size(),
            // The game writes the description, so it holds spaces. Quote it and keep it last.
            "ev=probe stage=hitch set=%s idx=%zu slot=%d tid=0x%08X kind=%u age=%.1f desc=\"%s\"",
            set,
            index,
            *reinterpret_cast<const std::int32_t*>(entry + kEntrySlotOffset),
            *reinterpret_cast<const std::uint32_t*>(entry + kEntryThreadOffset),
            static_cast<unsigned>(*reinterpret_cast<const std::uint8_t*>(entry + kEntryKindOffset)),
            static_cast<double>(*reinterpret_cast<const float*>(entry + kEntryAgeOffset)),
            text.data());
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
}

/** Dumps one filled snapshot, rate-limited and capped. */
void report(const std::byte* snapshot) noexcept {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t due = g_nextReportTick.load(std::memory_order_relaxed);
    if (now < due
        || !g_nextReportTick.compare_exchange_strong(
            due, now + kReportIntervalMs, std::memory_order_relaxed)) {
        return;
    }
    if (g_reports.fetch_add(1, std::memory_order_relaxed) >= kMaxReports) {
        return;
    }
    report_fibers(snapshot);
    report_entries(snapshot, kWaitEntriesOffset, "wait");
    report_entries(snapshot, kRingEntriesOffset, "ring");
    // The first used wait record carries the suspected thread; its stack names the wait site.
    for (std::size_t index = 0; index < kEntryCount; ++index) {
        const std::byte* const entry = snapshot + kWaitEntriesOffset + index * kEntryStride;
        std::uint32_t typeId = 0;
        std::memcpy(&typeId, entry + kEntryDescOffset + kDescTypeOffset, sizeof typeId);
        if (typeId == 0) {
            continue;
        }
        std::uint32_t tid = 0;
        std::memcpy(&tid, entry + kEntryThreadOffset, sizeof tid);
        if (tid != 0 && tid != 0xFFFFFFFFU) {
            report_stack(tid);
        }
        break;
    }
}

/** Lets the fill run, then dumps the snapshot it filled. */
__declspec(noinline) std::int64_t __fastcall fill_snapshot(std::byte* snapshot,
                                                           std::uint64_t a2,
                                                           std::uint64_t a3,
                                                           std::uint64_t a4) noexcept {
    const auto original = reinterpret_cast<Fill>(g_handle.original);
    if (original == nullptr) {
        return 0;
    }
    const std::int64_t result = original(snapshot, a2, a3, a4);
    if (snapshot != nullptr) {
        report(snapshot);
    }
    return result;
}

/** @param reason Short name of the step that failed. @return Always false. */
[[nodiscard]] bool fail(const char* reason) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=probe stage=hitch result=fail reason=%s", reason);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return false;
}

} // namespace

/** Resolves the printer, the fiber poll and the fill target, then attaches the one detour. */
bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    std::byte* const format = scan_main_image_unique(kFormat, "hitch_probe_format");
    if (format == nullptr) {
        return fail("hitch_probe_format");
    }
    g_lookup = reinterpret_cast<DescLookup>(
        resolve_relative(format + kLookupOperand, format + kLookupNext));
    g_print =
        reinterpret_cast<DescPrint>(resolve_relative(format + kPrintOperand, format + kPrintNext));
    if (g_lookup == nullptr || g_print == nullptr) {
        return fail("hitch_probe_format_operands");
    }
    std::byte* const poll = scan_main_image_unique(kGatePoll, "hitch_probe_gate_poll");
    if (poll == nullptr) {
        return fail("hitch_probe_gate_poll");
    }
    g_gate = reinterpret_cast<const std::uint32_t*>(
        resolve_relative(poll + kGateOperand, poll + kGateNext));
    g_fiberA = reinterpret_cast<const std::uint32_t*>(
        resolve_relative(poll + kFiberAOperand, poll + kFiberANext));
    g_fiberB = reinterpret_cast<const std::uint32_t*>(
        resolve_relative(poll + kFiberBOperand, poll + kFiberBNext));
    g_fiberC = reinterpret_cast<const std::uint32_t*>(
        resolve_relative(poll + kFiberCOperand, poll + kFiberCNext));
    g_fiberIdle =
        reinterpret_cast<FiberIdle>(resolve_relative(poll + kIdleOperand, poll + kIdleNext));
    if (g_gate == nullptr || g_fiberA == nullptr || g_fiberB == nullptr || g_fiberC == nullptr
        || g_fiberIdle == nullptr) {
        return fail("hitch_probe_gate_operands");
    }
    std::byte* const target = scan_main_image_unique(kFill, "hitch_probe_fill");
    if (target == nullptr) {
        return fail("hitch_probe_fill");
    }
    resolve_module_ranges();
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&fill_snapshot)};
    if (!hooking::detour::install(spec, g_handle)) {
        return fail("attach");
    }
    g_installed.store(true, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=probe stage=hitch result=installed");
    return true;
}

/** Detaches the snapshot dump and clears every resolved pointer. */
bool uninstall() noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    if (!hooking::detour::uninstall(g_handle)) {
        return false;
    }
    g_lookup = nullptr;
    g_print = nullptr;
    g_fiberIdle = nullptr;
    g_gate = nullptr;
    g_fiberA = nullptr;
    g_fiberB = nullptr;
    g_fiberC = nullptr;
    g_installed.store(false, std::memory_order_release);
    return true;
}

} // namespace sunrise::client::hooks::hitch_probe
