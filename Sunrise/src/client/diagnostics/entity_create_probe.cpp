#include "entity_create_probe.h"

#include <Windows.h>
#include <intrin.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../core/logging/log.h"
#include "../hooking/detour.h"
#include "../patterns/image_scan.h"
#include "../patterns/signature_text.h"

namespace sunrise::client::diagnostics {
namespace {

namespace patterns = client::patterns;
namespace detour = client::hooking::detour;

/**
 * The index allocator the entity creator calls first.
 * Recovered from the mapped-image dump. Its body is unmistakable: it stores -1 into the caller's
 * out-parameter, then asks a pool at `+0xC118` sized `0x2000` for a free index. The frame size is
 * wildcarded so the match carries no position-dependent byte.
 */
constexpr std::string_view kIndexAllocatorText =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC ? 48 8B DA C7 02 FF FF FF FF 48 8B F9 "
    "BA 00 20 00 00";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kIndexAllocator =
    patterns::signature<patterns::signature_length(kIndexAllocatorText)>(kIndexAllocatorText);

/** The allocator answers this in its out-parameter when it has no index to give. */
constexpr std::int32_t kNoIndex = -1;
/**
 * Byte offset of the free-slot bitmap inside the manager the allocator is handed.
 * Read out of the allocator's body: it calls the bitmap search with `rcx = manager + 0xC118` and
 * a width of `0x2000`, then clears the bit it was given. A set bit is therefore a FREE slot, and
 * the search answers -1 only when every word is zero.
 */
constexpr std::size_t kFreeBitmapOffset = 0xC118;
/** Slots the bitmap covers, from the width the allocator passes. */
constexpr std::size_t kFreeBitmapBits = 0x2000;
/** Words in that bitmap. */
constexpr std::size_t kFreeBitmapWords = kFreeBitmapBits / 32;

/**
 * Counts the free slots the manager currently holds.
 * The exhaustion line alone cannot separate "the host never gave the client any slots" from
 * "the client used everything it was given", and those need opposite fixes.
 * @param pool Manager the allocator was handed.
 * @return Set bits in its free bitmap, or -1 when the bitmap cannot be read.
 */
[[nodiscard]] std::int64_t free_slot_count(const void* pool) noexcept {
    if (pool == nullptr) {
        return -1;
    }
    std::int64_t free = 0;
    __try {
        const auto* words = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::byte*>(pool) + kFreeBitmapOffset);
        for (std::size_t word = 0; word < kFreeBitmapWords; ++word) {
            free += static_cast<std::int64_t>(__popcnt(words[word]));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return free;
}
/** Outcomes reported per run, so a per-frame failure cannot fill the log. */
constexpr LONG kMaxReports = 200;
/**
 * Stack frames captured above this probe on each allocation.
 * The allocator itself is generic — one function serves every entity in the game — so its own
 * address says nothing about what is being built. The callers above it are what differ, and six
 * frames is enough to separate "the world is placing an object" from "a weapon spawned a
 * projectile" without unwinding the whole fiber stack.
 */
constexpr ULONG kTraceFrames = 6;
/**
 * Allocation traces per run.
 * A raid load builds a few hundred entities, so this holds several bubble loads while still
 * bounding what a long firefight can write.
 */
constexpr LONG kMaxTraces = 4096;
/** Traces already spent. */
volatile LONG g_traces{};
/**
 * Image offset of the pointer to the game's entity record table.
 * Recovered from the creation path itself, which indexes it as `base + (handle & 0x1FFF) * stride`
 * at `0x4D71F7`: `imul ebx, [rip -> 0x1F93430]` then `add rbx, [rip -> 0x1F93428]`. The mask is the
 * same 13 bits the allocator's bitmap covers, so a record addresses exactly one allocated index.
 */
constexpr std::uintptr_t kEntityTableBaseRva = 0x1F93428;
/** Image offset of the record stride that pairs with the table above. */
constexpr std::uintptr_t kEntityTableStrideRva = 0x1F93430;
/** Stride the dump reports. Checked at runtime, because a wrong one would read foreign memory. */
constexpr std::uint32_t kExpectedRecordStride = 224;
/** Bytes of each record dumped. The whole record, so the type field can be found by comparison. */
constexpr std::size_t kRecordDumpBytes = kExpectedRecordStride;
/** Records dumped per run, bounded so a long session cannot fill the sink. */
constexpr LONG kMaxRecords = 512;
/** Records already dumped. */
volatile LONG g_records{};
/**
 * Record class every live entity carries at `+0x64`.
 * Constant across all 57 records of a run, so it marks a slot the game has actually built rather
 * than one holding whatever the last entity left behind.
 */
constexpr std::uint32_t kRecordClass = 0x80809783;
/** Offset of the record class within a record. */
constexpr std::size_t kRecordClassOffset = 0x64;
/** Offset of the object's definition hash. Varies per object kind; `0xFFFFFFFF` where absent. */
constexpr std::size_t kRecordDefinitionOffset = 0x88;
/** Offset of the instance ordinal that counts copies of one definition. */
constexpr std::size_t kRecordOrdinalOffset = 0x8C;
/** Offset of the transform block, which is still unset when a record is first dumped. */
constexpr std::size_t kRecordTransformOffset = 0xA0;
/** Dwords of the transform block reported, covering the orientation and position quads. */
constexpr std::size_t kRecordTransformDwords = 8;
/** Seconds between censuses. Short enough to catch a bubble soon after it settles. */
constexpr DWORD kCensusIntervalMs = 15'000;
/**
 * Most recent manager the allocator was handed.
 * The census needs the free bitmap to tell a live record from one an entity left behind, and the
 * allocator is the only place the manager pointer is known.
 */
void* volatile g_lastPool{};
/** Entries one census reports, so a fully populated table cannot fill the sink. */
constexpr LONG kCensusEntryBudget = 2'048;
/**
 * Distinct record classes counted per census.
 * The census filtered on one class, `kRecordClass`, and so never reported an index above ~1019.
 * An interaction incident then named entity **3539** as its target while the player stood on the
 * Wall of Wishes activation plate -- an object that works -- and the twenty panels that do not
 * work sit at 749..768. Whatever separates them is not visible while the walk only ever admits
 * one class, so every class is counted and sampled now.
 */
constexpr std::size_t kClassCapacity = 24;
/**
 * Records dumped per distinct class, so a large class cannot crowd out a small one.
 * Set at 48 this hid the very thing it was built to find: one class holds every real record, so
 * only indices 0..47 were ever dumped and the Wall of Wishes panels at 749..768 fell outside the
 * log entirely. That absence then read as "the player never reached the wall", which was wrong.
 * The share only needs to stop one class starving another, so it sits at the whole budget.
 */
constexpr LONG kPerClassDump = 2'048;
/** Cleared to stop the census thread. */
volatile LONG g_censusRunning{};
/** Census thread handle. */
HANDLE g_censusThread{};

/**
 * Index whose record has not been dumped yet.
 * The record is empty when the allocator hands the index out — the creator fills it afterwards — so
 * each index is read one allocation late, when whatever built it has finished.
 */
volatile LONG g_pendingIndex{-1};

/**
 * Dumps one entity record so the entity can be named rather than counted.
 * Counting proved the pool works and says nothing about what is in it. The record is the only place
 * the client keeps an entity's identity, and every entity in the run shares one creation path, so
 * the bytes here are what separate a wall panel from a projectile.
 * @param index Index whose record to read.
 */
void report_record(std::int32_t index) noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= kFreeBitmapBits
        || !core::log::accepts(core::log::Channel::client, core::log::Level::debug)
        || InterlockedIncrement(&g_records) > kMaxRecords) {
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (base == 0) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    int written = 0;
    __try {
        const auto table = *reinterpret_cast<const std::byte* const*>(base + kEntityTableBaseRva);
        const auto stride = *reinterpret_cast<const std::uint32_t*>(base + kEntityTableStrideRva);
        // A stride that has moved means this offset no longer names the table, and reading through
        // it would dump unrelated memory as if it were an entity.
        if (table == nullptr || stride != kExpectedRecordStride) {
            return;
        }
        const auto* const record = table + static_cast<std::size_t>(index) * stride;
        written = std::snprintf(line.data(),
                                line.size(),
                                "ev=entity_create stage=record idx=%d hex=",
                                static_cast<int>(index));
        for (std::size_t offset = 0; offset < kRecordDumpBytes && written > 0
                                     && static_cast<std::size_t>(written) + 3 < line.size();
             ++offset) {
            const int more = std::snprintf(line.data() + written,
                                           line.size() - static_cast<std::size_t>(written),
                                           "%02X",
                                           std::to_integer<unsigned char>(record[offset]));
            if (more <= 0) {
                break;
            }
            written += more;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (written <= 0) {
        return;
    }
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(core::log::Channel::client, core::log::Level::debug, {line.data(), length});
}
/** Resolved `RtlCaptureStackBackTrace`, or null when ntdll would not give it up. */
USHORT(NTAPI* g_captureBacktrace)(ULONG, ULONG, PVOID*, PULONG){};
/** Allocations between pool samples. Frequent enough to shape the drain, rare enough to be free. */
constexpr LONG kSampleInterval = 16;
/** Bytes in the bitmap, from the width the client's own stocking path passes to its fill. */
constexpr std::size_t kFreeBitmapBytes = kFreeBitmapBits / 8;
/**
 * High slots the host keeps for its own entities and never leases to the client.
 * The join grant is `kSlotCount - kDefaultServerReserve` = 7936, so the top 256 indices are the
 * host's. The client's own initialiser frees the whole bitmap because in its intended world it
 * owns every slot; here it does not, and handing it the reserve would let it allocate an index
 * the host also considers its own.
 */
constexpr std::size_t kServerReserveSlots = 256;
/** Bytes of the bitmap that stay clear, covering the reserve at the top of the index space. */
constexpr std::size_t kReserveBytes = kServerReserveSlots / 8;
/** Bytes of the bitmap that are freed to the client. */
constexpr std::size_t kClientBytes = kFreeBitmapBytes - kReserveBytes;
/** Words of the bitmap covering the client's half. The split lands on a word boundary. */
constexpr std::size_t kClientWords = kClientBytes / sizeof(std::uint32_t);
static_assert(kClientBytes % sizeof(std::uint32_t) == 0,
              "the client half must end on a word so a refill never touches the reserve");
/** Bits per bitmap word. */
constexpr std::size_t kBitsPerWord = 32;
/**
 * Address span treated as belonging to the game's image.
 * The dump reports an image size of 0x8A5EA00, so this clears it with room for a larger build while
 * still rejecting a frame that landed in Sunrise's own module or on a foreign allocation.
 */
constexpr std::uintptr_t kImageSpan = 0x10000000;

/**
 * The allocator's real shape, read from its body rather than guessed.
 * It uses exactly two arguments: `rcx` is the manager whose free-slot bitmap sits at `+0xC118`,
 * and `rdx` is the out-parameter it fills with the allocated index. It returns `rdx` unchanged.
 */
using IndexAllocator = void*(__fastcall*)(void*, std::int32_t*) noexcept;

detour::Handle g_allocator{};
volatile LONG g_reports{};
/** Successful allocations seen, used only to space the samples. */
volatile LONG g_allocations{};
/**
 * One manager's record of the indices this probe has watched the allocator hand out.
 *
 * A blanket `memset(bitmap, 0xFF, ...)` is what made the very first stocking work and what made
 * every later one lethal. It frees index 0 upward, and by the time a pool has drained, index 0
 * belongs to a live entity. The allocator picks the lowest set bit, so the next creation lands on
 * top of a live entity and the world stops being a consistent list of them. That is the crash on
 * respawn, the crash on Worldline Zero's ability, and the mainloop stall that ends a Shuro Chi run
 * a few seconds after the room loads.
 *
 * Keeping the set of indices already handed out turns the refill from "free everything" into
 * "free what was never taken", which is the only form of it that is safe to run on a live pool.
 */
struct PoolRecord {
    /** Manager this record belongs to, or null while the slot is unused. */
    void* pool;
    /** Set bit per index the allocator gave out and the client has not since handed back. */
    std::array<volatile LONG, kFreeBitmapWords> live;
    /** Whether this pool has been refilled at least once. */
    volatile LONG stocked;
};

/** Managers tracked at once. A world change builds a new one, so several are live per run. */
constexpr std::size_t kTrackedPoolCapacity = 16;
/** Per-manager occupancy records, claimed on first sight. */
std::array<PoolRecord, kTrackedPoolCapacity> g_pools{};

/**
 * Finds the record for one manager, claiming a free slot on first sight.
 * @param pool Manager the allocator was handed.
 * @return Its record, or null when the table is full.
 */
[[nodiscard]] PoolRecord* find_pool(void* pool) noexcept {
    for (auto& record : g_pools) {
        if (record.pool == pool) {
            return &record;
        }
    }
    for (auto& record : g_pools) {
        auto* const slot = reinterpret_cast<void* volatile*>(&record.pool);
        if (InterlockedCompareExchangePointer(slot, pool, nullptr) == nullptr
            || record.pool == pool) {
            return &record;
        }
    }
    // Past capacity nothing is tracked, so nothing is refilled either. A missed refill costs this
    // world's entities; an untracked one corrupts a live pool.
    return nullptr;
}

/**
 * Records that one index is now owned by an entity.
 * @param record Manager record, or null when the manager is untracked.
 * @param index Index the allocator produced.
 */
void mark_live(PoolRecord* record, std::int32_t index) noexcept {
    if (record == nullptr || index < 0 || static_cast<std::size_t>(index) >= kFreeBitmapBits) {
        return;
    }
    const auto slot = static_cast<std::size_t>(index);
    (void)InterlockedOr(&record->live[slot / kBitsPerWord],
                        static_cast<LONG>(1u << (slot % kBitsPerWord)));
}
/** Off leaves the probe reporting only, which is what it did before it could write. */
bool g_stockUnstockedPool{};
/** Refill a drained pool as well as an unstocked one. Safe now that the refill spares live slots. */
bool g_restockAlways{};

/**
 * Reports one probe outcome, up to the per-run budget.
 * @param stage Which half answered.
 * @param outcome What it answered.
 * @param detail Free slots left in the pool, or -1 when the bitmap could not be read.
 */
void report_pair(const char* stage,
                 const char* outcome,
                 std::int64_t detail,
                 std::int64_t allocations) noexcept {
    if (!core::log::accepts(core::log::Channel::client, core::log::Level::debug)
        || InterlockedIncrement(&g_reports) > kMaxReports) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=entity_create stage=%s result=%s free=%lld allocs=%lld",
                                      stage,
                                      outcome,
                                      static_cast<long long>(detail),
                                      static_cast<long long>(allocations));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Reports one refill, naming how many slots it actually handed back.
 * @param outcome Whether the pool answered after the refill.
 * @param free Free slots the bitmap holds now.
 * @param freed Slots this refill put back.
 * @param allocations Successful allocations seen so far.
 */
void report_stock(const char* outcome,
                  std::int64_t free,
                  std::int64_t freed,
                  std::int64_t allocations) noexcept {
    if (!core::log::accepts(core::log::Channel::client, core::log::Level::debug)
        || InterlockedIncrement(&g_reports) > kMaxReports) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=entity_create stage=allocate result=%s free=%lld freed=%lld allocs=%lld",
                      outcome,
                      static_cast<long long>(free),
                      static_cast<long long>(freed),
                      static_cast<long long>(allocations));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Names one allocation and the call sites that asked for it.
 *
 * Counting allocations proved the pool works; it cannot say what is being built, and that is the
 * question a missing Wall of Wishes actually poses. Its panels are one repeated object, so a burst
 * of identical traces landing on consecutive indices as a bubble loads is the wall being created,
 * and the absence of such a burst is the wall never being asked for. Those two need opposite fixes.
 *
 * Addresses are image-relative because the game is rebased every run; an RVA maps straight into the
 * mapped-image dump, where file offset equals RVA.
 * @param pool Manager the index came from, so per-type managers would show as distinct pointers.
 * @param index Index the allocator produced.
 * @param sequence Allocation ordinal within the run.
 */
void report_allocation(const void* pool, std::int32_t index, LONG sequence) noexcept {
    if (!core::log::accepts(core::log::Channel::client, core::log::Level::debug)
        || InterlockedIncrement(&g_traces) > kMaxTraces) {
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(line.data(),
                               line.size(),
                               "ev=entity_create stage=alloc n=%ld idx=%d pool=0x%llX sites=",
                               static_cast<long>(sequence),
                               static_cast<int>(index),
                               static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(pool)));
    if (written <= 0) {
        return;
    }
    std::array<void*, kTraceFrames> frames{};
    // Frame 0 is this probe, which is never interesting, so the capture starts one above it.
    const USHORT captured = g_captureBacktrace == nullptr
                                ? 0
                                : g_captureBacktrace(1, kTraceFrames, frames.data(), nullptr);
    for (USHORT frame = 0; frame < captured && written > 0
                           && static_cast<std::size_t>(written) < line.size();
         ++frame) {
        const auto site = reinterpret_cast<std::uintptr_t>(frames[frame]);
        // A frame inside Sunrise's own module is noise here; only the game's code is addressable
        // in the dump, so anything outside it is printed as a gap rather than a misleading offset.
        const bool inImage = base != 0 && site >= base && (site - base) < kImageSpan;
        const int more =
            std::snprintf(line.data() + written,
                          line.size() - static_cast<std::size_t>(written),
                          inImage ? "%s0x%llX" : "%s-",
                          frame == 0 ? "" : ",",
                          static_cast<unsigned long long>(inImage ? site - base : 0));
        if (more <= 0) {
            break;
        }
        written += more;
    }
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(core::log::Channel::client, core::log::Level::debug, {line.data(), length});
}

void report(const char* stage, const char* outcome, std::int64_t detail) noexcept {
    if (!core::log::accepts(core::log::Channel::client, core::log::Level::debug)
        || InterlockedIncrement(&g_reports) > kMaxReports) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=entity_create stage=%s result=%s free=%lld",
                                      stage,
                                      outcome,
                                      static_cast<long long>(detail));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Mirrors the index allocator and reports whether it produced an index.
 * The out-parameter is the answer: the original writes -1 into it before doing anything, and
 * overwrites it only on success.
 */
/**
 * Reads the game's entity record table, or reports that it cannot be addressed.
 * @param table Receives the table base.
 * @param stride Receives the record stride.
 * @return True when both were read and the stride still matches this build.
 */
[[nodiscard]] bool entity_table(const std::byte*& table, std::uint32_t& stride) noexcept {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (base == 0) {
        return false;
    }
    __try {
        table = *reinterpret_cast<const std::byte* const*>(base + kEntityTableBaseRva);
        stride = *reinterpret_cast<const std::uint32_t*>(base + kEntityTableStrideRva);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return table != nullptr && stride == kExpectedRecordStride;
}

/**
 * Reports which of one word's 32 indices already hold an entity.
 *
 * The probe's own record of handed-out indices covers only what came through the hooked allocator,
 * and a census measured that as 58 of 830 — the world's placed objects reach the table by some
 * other path entirely. Trusting that record alone therefore freed 7936 slots while 42 entities
 * were sitting in them, and the client then allocated straight over the top. The game's own record
 * table is the authority on which slots are taken, so occupancy is read from there instead.
 * @param table Entity record table base.
 * @param stride Record stride.
 * @param word Word of the free bitmap being refilled.
 * @return Set bit per index in that word whose record is live.
 */
[[nodiscard]] LONG occupied_mask(const std::byte* table, std::uint32_t stride, std::size_t word) noexcept {
    std::uint32_t mask = 0;
    for (std::size_t bit = 0; bit < kBitsPerWord; ++bit) {
        const std::size_t index = word * kBitsPerWord + bit;
        __try {
            if (*reinterpret_cast<const std::uint32_t*>(table + index * stride
                                                        + kRecordClassOffset)
                == kRecordClass) {
                mask |= 1u << bit;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // An unreadable record is treated as taken, which costs a slot rather than an entity.
            mask |= 1u << bit;
        }
    }
    return static_cast<LONG>(mask);
}

/**
 * Frees every client slot that no entity holds, leaving the ones that do alone.
 *
 * The client's own initialiser at `0x7FF71DDADB20` fills this bitmap with `0xFF` — every slot free
 * — but only when a role global reads zero; here it reads 3, so the fill never runs and the bitmap
 * is all-zero from the first frame. Every entity creation then fails, which is why no enemy, plate,
 * door or banner ever appeared and why an encounter bubble kicked to orbit. Writing those bytes
 * ourselves is right exactly once, on a pool that is still empty. On a pool that has drained it is
 * catastrophic, because the slots the client is using read as clear too and become free again.
 *
 * So the refill is driven by `record->live` instead of by a constant. A slot is freed only when the
 * bitmap says it is taken AND this probe never watched the allocator hand it out. Two passes,
 * because another thread may claim a slot while the first one runs: the second re-clears anything
 * that became live in between, so no index is ever offered twice.
 * @param pool Manager the allocator was handed.
 * @param record Occupancy record for that manager.
 * @param failure Receives a Windows error, or 1 for a null pool and 2 for a faulting write.
 * @return Slots freed, or -1 when the bitmap could not be written.
 */
[[nodiscard]] std::int64_t stock_pool(void* pool,
                                      PoolRecord* record,
                                      std::uint32_t& failure) noexcept {
    failure = 0;
    if (pool == nullptr || record == nullptr) {
        failure = 1;
        return -1;
    }
    auto* const bitmap = static_cast<std::byte*>(pool) + kFreeBitmapOffset;
    // The bitmap sits in the game's own allocation, so it carries whatever protection that
    // allocation was given. Reading it worked, which does not prove it is writable.
    DWORD previous = 0;
    if (VirtualProtect(bitmap, kFreeBitmapBytes, PAGE_READWRITE, &previous) == FALSE) {
        failure = GetLastError();
        return -1;
    }
    const std::byte* table = nullptr;
    std::uint32_t stride = 0;
    const bool hasTable = entity_table(table, stride);
    std::int64_t freed = 0;
    __try {
        auto* const words = reinterpret_cast<volatile LONG*>(bitmap);
        for (std::size_t word = 0; word < kClientWords; ++word) {
            const LONG available = words[word];
            // A slot the client has put back is no longer live, so it returns to the pool with the
            // rest. Without this the record would only ever grow and the refill would fade to a
            // no-op over a long session.
            const LONG live = InterlockedAnd(&record->live[word], ~available) & ~available;
            // The record table is the authority; the probe's own list is kept as a second opinion
            // for anything created in the window before its record is filled in.
            const LONG occupied = hasTable ? occupied_mask(table, stride, word) : 0;
            const LONG missing =
                static_cast<LONG>(~static_cast<std::uint32_t>(live | available | occupied));
            if (missing != 0) {
                (void)InterlockedOr(&words[word], missing);
                freed += __popcnt(static_cast<unsigned int>(missing));
            }
        }
        for (std::size_t word = 0; word < kClientWords; ++word) {
            const LONG live = record->live[word];
            if (live != 0) {
                (void)InterlockedAnd(&words[word], ~live);
            }
        }
        // The host's reserve at the top of the space stays clear so the client cannot allocate an
        // index the host also considers its own.
        for (std::size_t word = kClientWords; word < kFreeBitmapWords; ++word) {
            (void)InterlockedAnd(&words[word], 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        failure = 2;
        freed = -1;
    }
    DWORD restored = 0;
    (void)VirtualProtect(bitmap, kFreeBitmapBytes, previous, &restored);
    if (freed >= 0) {
        (void)InterlockedExchange(&record->stocked, 1);
        if (!hasTable) {
            // Worth saying out loud: without the table the refill is back to trusting a list that
            // has been measured as 7% complete, which is how live entities got overwritten.
            report("allocate", "stock_without_table", freed);
        }
    }
    return freed;
}

void* __fastcall allocator_body(void* pool, std::int32_t* index) noexcept {
    const auto call = reinterpret_cast<IndexAllocator>(g_allocator.original);
    if (call == nullptr) {
        return nullptr;
    }
    void* result = call(pool, index);
    InterlockedExchangePointer(&g_lastPool, pool);
    PoolRecord* const record = find_pool(pool);
    if (index == nullptr || *index != kNoIndex) {
        // Every index the client takes is recorded before anything else can act on it, because a
        // refill that does not know about it would offer the same index to a second entity.
        if (index != nullptr) {
            mark_live(record, *index);
        }
        // Sample the pool as it is spent. A steadily falling count means indices are allocated and
        // never returned; a count that rises again means the client's own free path does work and
        // the drain is simply the world being large. Those need opposite fixes, and the exhaustion
        // line alone cannot tell them apart because it only ever fires at zero.
        const LONG seen = InterlockedIncrement(&g_allocations);
        report_allocation(pool, index == nullptr ? kNoIndex : *index, seen);
        // One allocation behind, so the creator has had time to fill the record being read.
        report_record(InterlockedExchange(&g_pendingIndex, index == nullptr ? -1 : *index));
        if ((seen % kSampleInterval) == 0) {
            // The count is reported beside the free total: if the pool empties while this barely
            // moves, the bitmap is being cleared by something other than allocation.
            report_pair("allocate", "sample", free_slot_count(pool), seen);
        }
        return result;
    }
    const std::int64_t free = free_slot_count(pool);
    // A pool is refilled the first time it is seen empty, and again on every later drain when the
    // knob is on. Both are safe now: the refill spares the indices already handed out, so it can
    // no longer hand one index to two entities the way the old blanket fill did.
    const bool allowed = g_stockUnstockedPool && record != nullptr
                         && (g_restockAlways || record->stocked == 0);
    if (free != 0 || !allowed) {
        report_pair("allocate", "exhausted", free, g_allocations);
        return result;
    }
    std::uint32_t failure = 0;
    const std::int64_t freed = stock_pool(pool, record, failure);
    if (freed < 0) {
        // Naming the reason matters: a refused write and a faulting page need different fixes.
        report("allocate", "stock_failed", static_cast<std::int64_t>(failure));
        return result;
    }
    result = call(pool, index);
    // `freed` is the number that matters. It should fall well short of the whole client half: the
    // gap is the live entities the old fill used to trample.
    report_stock(*index == kNoIndex ? "stocked_still_empty" : "stocked",
                 free_slot_count(pool),
                 freed,
                 g_allocations);
    if (index != nullptr) {
        mark_live(record, *index);
    }
    return result;
}

/**
 * Image offset of the pointer that reaches the game's entity pool descriptors.
 * From the creation path at `0x4D71B5`: `mov rcx, [rip -> 0x2439C70]` then `add rdx, [rcx]` with
 * the pool ordinal already shifted left by six, so descriptors are 64 bytes apart and their array
 * base is one further dereference in. Within a descriptor, `+0x08` is the pool base and `+0x30`
 * its element size -- `imul eax, [rdx + 0x30]` then `add rcx, [rdx + 8]`.
 */
constexpr std::uintptr_t kPoolDirectoryRva = 0x2439C70;
/** Bytes between pool descriptors. */
constexpr std::size_t kPoolDescriptorStride = 64;
/** Descriptors probed. The ordinal comes from a handle's high bits, which are six wide. */
constexpr std::size_t kPoolDescriptorCount = 64;
/** Offset of a pool's base pointer within its descriptor. */
constexpr std::size_t kPoolBaseOffset = 0x08;
/** Offset of a pool's element size within its descriptor. */
constexpr std::size_t kPoolElementSizeOffset = 0x30;
/** An element size outside this is not a record, so the descriptor is not one either. */
constexpr std::uint32_t kMaximumElementSize = 4096;
/**
 * Pools whose elements match the entity record stride, walked by the census.
 * The directory holds TWO 224-byte pools, ordinals 33 and 35, at stable and distinct bases. The
 * census has only ever read whichever one `kEntityTableBaseRva` points at, so half the records of
 * this shape were never looked at -- and the activation plate that works, entity 3539, is not in
 * the half that was.
 */
constexpr std::size_t kRecordPoolCapacity = 4;
/** Bases of the record-shaped pools found in the directory. */
std::array<const std::byte*, kRecordPoolCapacity> g_recordPools{};
/** Ordinals of those pools, in the same order. */
std::array<std::size_t, kRecordPoolCapacity> g_recordPoolOrdinals{};
/** Record-shaped pools found. */
std::size_t g_recordPoolCount{};

/**
 * Reports every entity pool the game keeps, not just the one the census walks.
 *
 * The class tally proved the 224-byte table holds exactly one class and 830 records, and that
 * everything read above them is out-of-bounds noise. So the Wall of Wishes activation plate, which
 * an interaction incident named as entity 3539 and which visibly works, cannot be in that table at
 * all -- while the twenty panels that do not work are. Handles carry a pool ordinal in their high
 * bits, which is why one table was never the whole picture.
 */
void report_pools() noexcept {
    if (!core::log::accepts(core::log::Channel::client, core::log::Level::debug)) {
        return;
    }
    const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (image == 0) {
        return;
    }
    for (std::size_t ordinal = 0; ordinal < kPoolDescriptorCount; ++ordinal) {
        const std::byte* poolBase = nullptr;
        std::uint32_t elementSize = 0;
        __try {
            const auto* const directory =
                *reinterpret_cast<const std::byte* const*>(image + kPoolDirectoryRva);
            if (directory == nullptr) {
                return;
            }
            const auto* const descriptors = *reinterpret_cast<const std::byte* const*>(directory);
            if (descriptors == nullptr) {
                return;
            }
            const auto* const descriptor = descriptors + ordinal * kPoolDescriptorStride;
            poolBase = *reinterpret_cast<const std::byte* const*>(descriptor + kPoolBaseOffset);
            elementSize =
                *reinterpret_cast<const std::uint32_t*>(descriptor + kPoolElementSizeOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (poolBase == nullptr || elementSize == 0 || elementSize > kMaximumElementSize) {
            continue;
        }
        if (elementSize == kExpectedRecordStride && g_recordPoolCount < kRecordPoolCapacity) {
            g_recordPoolOrdinals[g_recordPoolCount] = ordinal;
            g_recordPools[g_recordPoolCount++] = poolBase;
        }
        std::array<char, core::log::kLineCapacity> line{};
        const int written =
            std::snprintf(line.data(),
                          line.size(),
                          "ev=entity_census stage=pool ordinal=%zu base=0x%llX element=%u",
                          ordinal,
                          static_cast<unsigned long long>(
                              reinterpret_cast<std::uintptr_t>(poolBase)),
                          elementSize);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::debug,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
}

/**
 * Reports the built records of one record-shaped pool other than the cached one.
 *
 * The cached pointer at `kEntityTableBaseRva` names a single pool, and the directory shows two of
 * this shape. An object that works and an object that does not may simply live in different pools,
 * and that is not visible while only one is read.
 * @param poolBase Base of the pool to walk.
 * @param stride Record stride, the same for every pool of this shape.
 * @param ordinal Directory ordinal, reported so the two can be told apart.
 */
void walk_pool(const std::byte* poolBase, std::uint32_t stride, std::size_t ordinal) noexcept {
    LONG reported = 0;
    for (std::size_t index = 0; index < kFreeBitmapBits && reported < kCensusEntryBudget; ++index) {
        std::array<char, core::log::kLineCapacity> line{};
        int written = 0;
        __try {
            const auto* const record = poolBase + index * stride;
            const auto recordClass =
                *reinterpret_cast<const std::uint32_t*>(record + kRecordClassOffset);
            if (recordClass != kRecordClass) {
                continue;
            }
            written = std::snprintf(
                line.data(),
                line.size(),
                "ev=entity_census stage=entry pool=%zu idx=%zu cls=0x%08X def=0x%08X ord=%u rec=",
                ordinal,
                index,
                recordClass,
                *reinterpret_cast<const std::uint32_t*>(record + kRecordDefinitionOffset),
                *reinterpret_cast<const std::uint32_t*>(record + kRecordOrdinalOffset));
            for (std::size_t offset = 0; offset < kRecordDumpBytes && written > 0
                                         && static_cast<std::size_t>(written) + 3 < line.size();
                 ++offset) {
                const int more = std::snprintf(line.data() + written,
                                               line.size() - static_cast<std::size_t>(written),
                                               "%02X",
                                               std::to_integer<unsigned char>(record[offset]));
                if (more <= 0) {
                    break;
                }
                written += more;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (written <= 0) {
            continue;
        }
        ++reported;
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    std::array<char, core::log::kLineCapacity> tail{};
    const int written = std::snprintf(tail.data(),
                                      tail.size(),
                                      "ev=entity_census stage=pool_end ordinal=%zu records=%ld",
                                      ordinal,
                                      static_cast<long>(reported));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {tail.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Walks the whole entity table and reports every slot the game has built.
 *
 * The per-allocation dump reads a record one allocation after it is handed out, which is early
 * enough that the transform is still its default — every instance of one definition reported the
 * same placement, which cannot be true. A census taken well after a bubble has settled reads the
 * finished records instead, and placement is the field that matters here: a grid of identical
 * co-planar objects is a wall of shootable panels and nothing else is, so this can identify the
 * Wall of Wishes without knowing the game's own name for it.
 */
void run_census() noexcept {
    if (!core::log::accepts(core::log::Channel::client, core::log::Level::debug)) {
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (base == 0) {
        return;
    }
    const std::byte* table = nullptr;
    std::uint32_t stride = 0;
    __try {
        table = *reinterpret_cast<const std::byte* const*>(base + kEntityTableBaseRva);
        stride = *reinterpret_cast<const std::uint32_t*>(base + kEntityTableStrideRva);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (table == nullptr || stride != kExpectedRecordStride) {
        return;
    }
    // A record keeps its class marker after the entity is gone, so the marker alone cannot
    // distinguish a live entity from a slot one left behind. The free bitmap can: a slot the
    // allocator would hand out is not holding anything, whatever its record still says.
    const auto* freeWords = static_cast<const std::uint32_t*>(nullptr);
    if (void* const pool = g_lastPool; pool != nullptr) {
        freeWords = reinterpret_cast<const std::uint32_t*>(static_cast<std::byte*>(pool)
                                                           + kFreeBitmapOffset);
    }
    // First pass counts every class present. A record whose class word is zero or all ones has
    // never been built, so those are the only two values treated as empty.
    std::array<std::uint32_t, kClassCapacity> classes{};
    std::array<LONG, kClassCapacity> classCounts{};
    std::array<LONG, kClassCapacity> classDumped{};
    std::size_t classCount = 0;
    for (std::size_t index = 0; index < kFreeBitmapBits; ++index) {
        std::uint32_t value = 0;
        __try {
            value = *reinterpret_cast<const std::uint32_t*>(table + index * stride
                                                            + kRecordClassOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (value == 0 || value == 0xFFFFFFFFU) {
            continue;
        }
        std::size_t slot = 0;
        while (slot < classCount && classes[slot] != value) {
            ++slot;
        }
        if (slot == classCount) {
            if (classCount == kClassCapacity) {
                continue;
            }
            classes[classCount++] = value;
        }
        ++classCounts[slot];
    }
    for (std::size_t slot = 0; slot < classCount; ++slot) {
        std::array<char, core::log::kLineCapacity> head{};
        const int headWritten = std::snprintf(head.data(),
                                              head.size(),
                                              "ev=entity_census stage=class value=0x%08X count=%ld",
                                              classes[slot],
                                              static_cast<long>(classCounts[slot]));
        if (headWritten > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::debug,
                             {head.data(), static_cast<std::size_t>(headWritten)});
        }
    }
    g_recordPoolCount = 0;
    report_pools();
    // Name the pool the census has been reading all along, so its ordinal can be matched against
    // the directory rather than assumed.
    {
        std::array<char, core::log::kLineCapacity> line{};
        const int written = std::snprintf(
            line.data(),
            line.size(),
            "ev=entity_census stage=table base=0x%llX stride=%u pools=%zu",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(table)),
            stride,
            g_recordPoolCount);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::debug,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
    // Every record-shaped pool, not just the cached one. A pool the cached pointer already names
    // is not walked twice.
    for (std::size_t slot = 0; slot < g_recordPoolCount; ++slot) {
        if (g_recordPools[slot] == table) {
            continue;
        }
        walk_pool(g_recordPools[slot], stride, g_recordPoolOrdinals[slot]);
    }
    LONG live = 0;
    LONG stale = 0;
    // Per pass, not per run: a shared budget truncated the one census that mattered.
    LONG entries = 0;
    for (std::size_t index = 0; index < kFreeBitmapBits; ++index) {
        std::array<char, core::log::kLineCapacity> line{};
        int written = 0;
        __try {
            const auto* const record = table + index * stride;
            const auto recordClass =
                *reinterpret_cast<const std::uint32_t*>(record + kRecordClassOffset);
            if (recordClass == 0 || recordClass == 0xFFFFFFFFU) {
                continue;
            }
            std::size_t slot = 0;
            while (slot < classCount && classes[slot] != recordClass) {
                ++slot;
            }
            const bool spent = slot == classCount || classDumped[slot] >= kPerClassDump;
            if (!spent) {
                ++classDumped[slot];
            }
            unsigned slotFree = 0;
            if (freeWords != nullptr
                && (freeWords[index / kBitsPerWord] & (1u << (index % kBitsPerWord))) != 0) {
                slotFree = 1;
                ++stale;
            } else {
                ++live;
            }
            // The tally above counts every record; only the dump is rationed.
            if (spent || entries >= kCensusEntryBudget) {
                continue;
            }
            written = std::snprintf(
                line.data(),
                line.size(),
                "ev=entity_census stage=entry idx=%zu cls=0x%08X def=0x%08X ord=%u free=%u rec=",
                index,
                recordClass,
                *reinterpret_cast<const std::uint32_t*>(record + kRecordDefinitionOffset),
                *reinterpret_cast<const std::uint32_t*>(record + kRecordOrdinalOffset),
                slotFree);
            // The whole record, not just the transform block. The block at `+0xA0` decodes as a
            // clean quaternion but the four dwords after it are not the position — as floats they
            // are denormals and values in the trillions. Somewhere in these 224 bytes there are
            // three coordinates, and the way to find them is to scan every aligned offset across a
            // group for one that varies plausibly. A 5x5 grid of co-planar panels is the Wall of
            // Wishes and nothing else in the room is shaped like that, so placement identifies it
            // where counting has not.
            for (std::size_t offset = 0; offset < kRecordDumpBytes && written > 0
                                         && static_cast<std::size_t>(written) + 3 < line.size();
                 ++offset) {
                const int more = std::snprintf(line.data() + written,
                                               line.size() - static_cast<std::size_t>(written),
                                               "%02X",
                                               std::to_integer<unsigned char>(record[offset]));
                if (more <= 0) {
                    break;
                }
                written += more;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (written <= 0) {
            continue;
        }
        ++entries;
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    std::array<char, core::log::kLineCapacity> tail{};
    const int written = std::snprintf(tail.data(),
                                      tail.size(),
                                      "ev=entity_census stage=end live=%ld stale=%ld allocs=%ld",
                                      static_cast<long>(live),
                                      static_cast<long>(stale),
                                      static_cast<long>(g_allocations));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {tail.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Runs a census on its own thread so it does not sit inside the game's allocation path.
 * @param unused Thread parameter, unused.
 * @return Always zero.
 */
DWORD WINAPI census_thread(LPVOID unused) noexcept {
    (void)unused;
    while (g_censusRunning != 0) {
        Sleep(kCensusIntervalMs);
        if (g_censusRunning == 0) {
            break;
        }
        run_census();
    }
    return 0;
}

/**
 * Attaches one probe, reporting its own outcome.
 * @param signature Pattern to find.
 * @param name Reported name.
 * @param replacement Probe body.
 * @param handle Receives the trampoline.
 * @return True when the target was found and the detour attached.
 */
[[nodiscard]] bool attach(std::span<const patterns::PatternByte> signature,
                          const char* name,
                          void* replacement,
                          detour::Handle& handle) noexcept {
    std::byte* const target = patterns::scan_main_image_unique(signature, name);
    std::array<char, core::log::kLineCapacity> line{};
    if (target == nullptr) {
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=entity_create stage=attach name=%s result=fail",
                                          name);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(written)});
        }
        return false;
    }
    const detour::Spec spec{target, replacement};
    const bool attached = detour::install(spec, handle);
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=entity_create stage=attach name=%s result=%s",
                                      name,
                                      attached ? "ok" : "fail");
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         attached ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return attached;
}

} // namespace

/** Reports which half of the client's entity creation refuses. */
bool install_entity_create_probe(bool stockUnstockedPool, bool restockAlways) noexcept {
    g_stockUnstockedPool = stockUnstockedPool;
    g_restockAlways = restockAlways;
    // Resolved rather than linked: the trace is a diagnostic, and a missing export should cost the
    // call sites in the log, not the probe that stocks the pool.
    if (HMODULE const ntdll = GetModuleHandleW(L"ntdll.dll"); ntdll != nullptr) {
        g_captureBacktrace = reinterpret_cast<decltype(g_captureBacktrace)>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlCaptureStackBackTrace")));
    }
    const bool allocator = attach(kIndexAllocator,
                                  "entity_index_allocator",
                                  reinterpret_cast<void*>(&allocator_body),
                                  g_allocator);
    if (allocator) {
        InterlockedExchange(&g_censusRunning, 1);
        g_censusThread = CreateThread(nullptr, 0, &census_thread, nullptr, 0, nullptr);
    }
    // The initialiser is deliberately NOT hooked. Its fifth argument is passed on the stack
    // (`mov dword [var_20h], eax` before the call), and a four-argument replacement got that
    // wrong and black-screened the load. It does not need hooking anyway: the allocator alone
    // answers the question, because the initialiser only runs when the allocator succeeded.
    return allocator;
}

/** Detaches the entity-creation probes. */
void uninstall_entity_create_probe() noexcept {
    InterlockedExchange(&g_censusRunning, 0);
    if (g_censusThread != nullptr) {
        // The census only reads, so a shutdown that beats it costs a census, never the process.
        (void)CloseHandle(g_censusThread);
        g_censusThread = nullptr;
    }
    if (g_allocator.attached) {
        (void)detour::uninstall(g_allocator);
    }
}

} // namespace sunrise::client::diagnostics
