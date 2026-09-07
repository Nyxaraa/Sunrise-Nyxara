/**
 * Bounded replacement for the sense-state extract's record-chain walk.
 * After a slice-set teardown the chain can hold freed records. The native walk then never ends,
 * and holds the frame graph shut. Extracting a freed record copies a garbage size to a garbage
 * destination. So the guard walks the chain dry first, and extracts only a chain that ended
 * with no repeated handle. A bad chain is logged instead.
 */

#include "sense_chain_guard.h"

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
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"

namespace sunrise::client::hooks::sense_chain_guard {
namespace {

using patterns::resolve_relative;
using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/** The per-list sense-state extract. Its second argument holds the chain head at `+4`. */
constexpr std::string_view kExtractText =
    "48 8B C4 53 48 83 EC 70 8B 5A 04 83 FB FF 0F 84 ? ? ? ? 48 8B 15 ? ? ? ? 48 89 68 08";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kExtract = signature<signature_length(kExtractText)>(kExtractText);

/** Handle-table storage operand inside the extract, and the next instruction. */
constexpr std::size_t kTablesOperand = 23;
constexpr std::size_t kTablesNext = 27;

/** The extract's five call sites, as offsets of each E8 from the matched start. */
constexpr std::size_t kAccessorCtorCall = 0x7F;
constexpr std::size_t kBindCall = 0x90;
constexpr std::size_t kSendExtractCall = 0xB5;
constexpr std::size_t kStructSizeCall = 0xD0;
constexpr std::size_t kPayloadCall = 0xDD;
constexpr std::byte kCallOpcode{0xE8};

/** Chain head handle inside the list argument. */
constexpr std::size_t kListHeadOffset = 4;
/** Record fields: reflection type id, changed flag, extracted flag, next-record handle. */
constexpr std::size_t kRecordTypeOffset = 8;
constexpr std::size_t kRecordChangedOffset = 111;
constexpr std::size_t kRecordExtractedOffset = 112;
constexpr std::size_t kRecordNextOffset = 120;
/** Sub-table fields the extract reads: row array pointer, row stride, row tag mask. */
constexpr std::uint64_t kSubRowArrayOffset = 8;
constexpr std::uint64_t kSubRowStrideOffset = 48;
constexpr std::uint64_t kSubRowTagMaskOffset = 52;
/** Tag qword inside a row; the masked part is subtracted to reach the record start. */
constexpr std::uint64_t kRowTagOffset = 8;
/** The no-record handle value ending a sane chain. */
constexpr std::uint32_t kNoRecord = 0xFFFFFFFFU;
/** The accessor is a 32-byte stack object in the original; this over-allocates it. */
constexpr std::size_t kAccessorCapacity = 64;
/** Far past any real record count; a walk this long is re-linking itself. */
constexpr std::size_t kChainCap = 4096;
/** Trailing chain handles kept for the runaway report; the cycle sits at the tail. */
constexpr std::size_t kReportHandles = 6;
/** One runaway line per interval, capped per process. The walk runs every tick. */
constexpr std::uint64_t kReportIntervalMs = 5'000;
constexpr unsigned kMaxReports = 64;

// A decompiler argument count is an inference, so every callee gets all four integer argument
// registers, filled with what the replaced walk's own register state held at each call.
using AccessorCtor = void*(__fastcall*)(void*, std::uint64_t, std::uint64_t, std::uint64_t);
using BindAccessor = bool(__fastcall*)(void*, std::byte*, std::uint64_t, std::uint64_t);
using SendExtract = std::int64_t(__fastcall*)(void*, void*, std::uint64_t, std::uint64_t);
using StructSize = const std::int32_t*(__fastcall*)(void*,
                                                    std::uint32_t,
                                                    std::uint64_t,
                                                    std::uint64_t);
using RecordPayload = std::byte*(__fastcall*)(std::byte*,
                                              std::uint32_t,
                                              std::uint64_t,
                                              std::uint64_t);

/** Out-pair the extract call fills: a payload handle and a byte offset into its datum. */
struct ExtractedRef {
    std::int64_t handle{-1};
    std::int64_t offset{0};
};

/** Ring slot value that means "never written", so a report cannot fake a zeroed record. */
constexpr std::uint32_t kUnwrittenSlot = 0xEEEEEEEEU;

/** Trailing ring of visited records, for the runaway report. */
struct WalkTrail {
    std::array<std::uint32_t, kReportHandles> handles{kUnwrittenSlot,
                                                      kUnwrittenSlot,
                                                      kUnwrittenSlot,
                                                      kUnwrittenSlot,
                                                      kUnwrittenSlot,
                                                      kUnwrittenSlot};
    std::array<std::uint32_t, kReportHandles> types{kUnwrittenSlot,
                                                    kUnwrittenSlot,
                                                    kUnwrittenSlot,
                                                    kUnwrittenSlot,
                                                    kUnwrittenSlot,
                                                    kUnwrittenSlot};
    std::uint32_t head{kUnwrittenSlot};
    std::size_t next{};
    std::size_t total{};
};

hooking::detour::Handle g_handle{};
std::atomic_bool g_installed{false};
const std::uint64_t* g_tables{};
AccessorCtor g_accessorCtor{};
BindAccessor g_bind{};
SendExtract g_sendExtract{};
StructSize g_structSize{};
RecordPayload g_payload{};
std::atomic<std::uint64_t> g_nextReportTick{0};
std::atomic<unsigned> g_reports{0};
/** Set while a walk runs. Two walks at once would share the chain buffer below. */
std::atomic_flag g_walking{};

/** Rebuilds a pointer from an address without an implementation-defined integer conversion. */
[[nodiscard]] std::byte* address_pointer(std::uint64_t address) noexcept {
    std::byte* pointer = nullptr;
    static_assert(sizeof pointer == sizeof address);
    std::memcpy(&pointer, &address, sizeof pointer);
    return pointer;
}

/** @return The value the game stores at one address. */
template <typename Value> [[nodiscard]] Value read_field(std::uint64_t address) noexcept {
    Value value{};
    std::memcpy(&value, address_pointer(address), sizeof value);
    return value;
}

/** @return The record a handle names, with the extract's own resolution. */
[[nodiscard]] std::byte* resolve_record(std::uint64_t tables, std::uint32_t handle) noexcept {
    const std::uint32_t high = static_cast<std::uint32_t>(static_cast<std::int32_t>(handle) >> 13);
    // The argument is the table object; the sub-table array pointer is its first qword.
    const std::uint64_t sub = read_field<std::uint64_t>(tables)
                              + (static_cast<std::uint64_t>(static_cast<std::uint16_t>(high)
                                                            & ((high | 0xFFC0000U) >> 18))
                                 << 6);
    const std::uint64_t row =
        read_field<std::uint64_t>(sub + kSubRowArrayOffset)
        + static_cast<std::uint32_t>((handle & 0x1FFFU)
                                     * read_field<std::uint32_t>(sub + kSubRowStrideOffset));
    return address_pointer(
        row
        - (read_field<std::uint64_t>(row + kRowTagOffset)
           & static_cast<std::int64_t>(read_field<std::int32_t>(sub + kSubRowTagMaskOffset))));
}

/** Extracts one record exactly as the replaced walk body does. POD frame only, for __try. */
void extract_record(std::uint64_t tables, std::byte* record) noexcept {
    alignas(16) unsigned char accessor[kAccessorCapacity] = {};
    // The replaced walk holds the table base in the second argument register at this call.
    g_accessorCtor(accessor, tables, 0, 0);
    record[kRecordExtractedOffset] = std::byte{0};
    if (!g_bind(accessor, record, 0, 0)) {
        return;
    }
    ExtractedRef ref{};
    g_sendExtract(accessor, &ref, 0, 0);
    const auto payloadHandle = static_cast<std::uint32_t>(ref.handle);
    if (payloadHandle == kNoRecord) {
        return;
    }
    std::uint64_t scratch = 0;
    const std::int32_t* const sizeStorage = g_structSize(
        &scratch, *reinterpret_cast<const std::uint32_t*>(record + kRecordTypeOffset), 0, 0);
    const auto size = static_cast<std::size_t>(*sizeStorage);
    std::byte* const destination = g_payload(record, static_cast<std::uint32_t>(size), 0, 0);
    const std::byte* const source = resolve_record(tables, payloadHandle) + ref.offset;
    if (std::memcmp(destination, source, size) != 0) {
        std::memcpy(destination, source, size);
        record[kRecordChangedOffset] = std::byte{1};
    }
    record[kRecordExtractedOffset] = std::byte{1};
}

/** Chain handles the dry pass collected. Written only while `g_walking` is held. */
std::uint32_t g_chain[kChainCap];

/**
 * Walks one chain dry, then extracts it only when it proved sane. POD frame only, for __try.
 * A revisited handle means a cycling chain of freed records; extracting one copies a garbage
 * size to a garbage destination, so a bad chain extracts nothing.
 * @return 1 done, 0 cap tripped, 2 revisited a record, -1 a step faulted.
 */
[[nodiscard]] int walk_and_extract(const std::byte* list, WalkTrail& trail) noexcept {
    __try {
        const std::uint64_t tables = g_tables != nullptr ? *g_tables : 0;
        if (tables == 0) {
            return -1;
        }
        std::uint32_t handle = *reinterpret_cast<const std::uint32_t*>(list + kListHeadOffset);
        trail.head = handle;
        while (handle != kNoRecord) {
            if (trail.total >= kChainCap) {
                return 0;
            }
            const std::byte* const record = resolve_record(tables, handle);
            trail.handles[trail.next] = handle;
            trail.types[trail.next] =
                *reinterpret_cast<const std::uint32_t*>(record + kRecordTypeOffset);
            trail.next = (trail.next + 1) % kReportHandles;
            for (std::size_t index = 0; index < trail.total; ++index) {
                if (g_chain[index] == handle) {
                    ++trail.total;
                    return 2;
                }
            }
            g_chain[trail.total] = handle;
            ++trail.total;
            handle = *reinterpret_cast<const std::uint32_t*>(record + kRecordNextOffset);
        }
        for (std::size_t index = 0; index < trail.total; ++index) {
            // The extract can move the datum, so resolve each record fresh, as the native
            // walk re-resolves before every read.
            extract_record(tables, resolve_record(tables, g_chain[index]));
        }
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

/** Logs one runaway or faulted walk with its trailing record handles and type ids. */
void report_walk(const WalkTrail& trail, const char* result) noexcept {
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
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=probe stage=sense result=%s steps=%zu head=0x%08X",
                                result,
                                trail.total,
                                trail.head);
    const std::size_t kept = trail.total < kReportHandles ? trail.total : kReportHandles;
    for (std::size_t index = 0; written > 0 && index < kept; ++index) {
        const std::size_t slot = (trail.next + kReportHandles - kept + index) % kReportHandles;
        const int piece = std::snprintf(line.data() + written,
                                        line.size() - static_cast<std::size_t>(written),
                                        " h%zu=0x%08X t%zu=0x%08X",
                                        index,
                                        trail.handles[slot],
                                        index,
                                        trail.types[slot]);
        if (piece <= 0) {
            break;
        }
        written += piece;
    }
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** Replaces the walk. A chain that fails the dry pass is reported and not extracted. */
__declspec(noinline) std::int64_t __fastcall sense_extract(std::byte* owner,
                                                           std::byte* list,
                                                           std::uint64_t a3,
                                                           std::uint64_t a4) noexcept {
    static_cast<void>(owner);
    static_cast<void>(a3);
    static_cast<void>(a4);
    if (list == nullptr) {
        return 0;
    }
    WalkTrail trail{};
    // A second walk while one runs extracts nothing this tick; the chain buffer is not shared.
    if (g_walking.test_and_set(std::memory_order_acquire)) {
        report_walk(trail, "busy");
        return 0;
    }
    const int outcome = walk_and_extract(list, trail);
    g_walking.clear(std::memory_order_release);
    if (outcome == 0) {
        report_walk(trail, "runaway");
    } else if (outcome == 2) {
        report_walk(trail, "cycle");
    } else if (outcome == -1) {
        report_walk(trail, "fault");
    }
    return 0;
}

/** Decodes one verified E8 call operand inside the matched extract. */
[[nodiscard]] std::byte* call_target(std::byte* target, std::size_t callOffset) noexcept {
    if (target[callOffset] != kCallOpcode) {
        return nullptr;
    }
    return resolve_relative(target + callOffset + 1, target + callOffset + 5);
}

} // namespace

/** Resolves the extract, its handle tables and its five callees, then attaches the detour. */
bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    std::byte* const target = scan_main_image_unique(kExtract, "sense_chain_extract");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=probe stage=sense result=fail reason=target");
        return false;
    }
    g_tables = reinterpret_cast<const std::uint64_t*>(
        resolve_relative(target + kTablesOperand, target + kTablesNext));
    g_accessorCtor = reinterpret_cast<AccessorCtor>(call_target(target, kAccessorCtorCall));
    g_bind = reinterpret_cast<BindAccessor>(call_target(target, kBindCall));
    g_sendExtract = reinterpret_cast<SendExtract>(call_target(target, kSendExtractCall));
    g_structSize = reinterpret_cast<StructSize>(call_target(target, kStructSizeCall));
    g_payload = reinterpret_cast<RecordPayload>(call_target(target, kPayloadCall));
    if (g_tables == nullptr || g_accessorCtor == nullptr || g_bind == nullptr
        || g_sendExtract == nullptr || g_structSize == nullptr || g_payload == nullptr) {
        g_tables = nullptr;
        g_accessorCtor = nullptr;
        g_bind = nullptr;
        g_sendExtract = nullptr;
        g_structSize = nullptr;
        g_payload = nullptr;
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=probe stage=sense result=fail reason=operand");
        return false;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&sense_extract)};
    if (!hooking::detour::install(spec, g_handle)) {
        g_tables = nullptr;
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=probe stage=sense result=fail reason=attach");
        return false;
    }
    g_installed.store(true, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=probe stage=sense result=installed");
    return true;
}

/** Detaches the guard and clears every resolved pointer. */
bool uninstall() noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    // The replacement reads the pointers below, so a thread still inside it refuses the detach.
    bool replacementActive = false;
    if (!hooking::detour::uninstall(g_handle, replacementActive)) {
        return false;
    }
    g_tables = nullptr;
    g_accessorCtor = nullptr;
    g_bind = nullptr;
    g_sendExtract = nullptr;
    g_structSize = nullptr;
    g_payload = nullptr;
    g_installed.store(false, std::memory_order_release);
    return true;
}

} // namespace sunrise::client::hooks::sense_chain_guard
