/**
 * A read-only probe on the client's activity msg 12 handler.
 * It reports the status word the handler writes. That word is the only thing in reach that
 * separates "the client never processed the body" from "it did, and the bind still failed".
 */

#include "membership_probe.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"

namespace sunrise::client::hooks::membership_probe {
namespace {

using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/**
 * `ActivityMsg12_ReplicateMembership_Recv`.
 * Its first argument is the ActivityClient. It commits the membership block at `+27696`, then
 * sets bit `0x100` of the status word at `+304` unconditionally, before returning 1.
 */
constexpr std::string_view kReceiveText = "40 55 53 41 56 41 57 48 8D AC 24 ? ? ? ? B8 C8 96 05 00";
constexpr auto kReceive = signature<signature_length(kReceiveText)>(kReceiveText);

/** Status word the handler writes, one bit per membership step. */
constexpr std::size_t kStatusWordOffset = 304;
/** Membership header. Its leading qword is the member key the client matches itself by. */
constexpr std::size_t kMembershipHeaderOffset = 27696;
/**
 * The two slot axes and the printable label the constructor builds from them.
 * Axis 1 is PRIVATE or PUBLIC, axis 2 is CURRENT or TARGET, indexed as `axis1 + 2 * axis2`. A
 * TARGET slot is index 2 or 3, which the public-first current-slot pick never reads.
 */
constexpr std::size_t kSlotAxisOneOffset = 24;
constexpr std::size_t kSlotAxisTwoOffset = 28;
constexpr std::size_t kSlotLabelOffset = 32;
/** The label is NUL-terminated; "PRIVATE CURRENT" is the longest form. */
constexpr std::size_t kSlotLabelCapacity = 32;
/**
 * Established session id, returned by the client's own vtable slot 1.
 * The rebind skips a slot outright when this is zero, with no log and no other symptom. Msg 4's
 * accept arm is its only writer, from `join_request` field 1 inside the `join_result` we send.
 */
constexpr std::size_t kEstablishedSessionOffset = 16352;
/**
 * Slot record this client belongs to. The map's target, laid out as `mgr + 720 * index + 10752`,
 * so the difference between two clients' values divided by 720 is their slot-record index delta.
 */
constexpr std::size_t kSlotRecordOffset = 27672;
/** The slot's roster container. The public-first current-slot pick requires it non-null. */
constexpr std::size_t kRosterContainerOffset = 27680;
/**
 * Sticky bind receipt. Zero from construction, one the moment a world container binds, and back
 * to zero only on a session reset. The grant dirty byte is cleared within a tick, so a sample can
 * miss it; this one cannot be missed.
 */
constexpr std::size_t kBindReceiptOffset = 27689;
/** Bit the handler sets, which the world-container bind and the player watcher both read. */
constexpr std::uint16_t kMembershipFlag = 0x100;
/** Entity-slot grant the client has taken but not yet applied to a view. 8192 bits. */
constexpr std::size_t kPendingMaskOffset = 392856;
constexpr std::size_t kPendingMaskSize = 1024;
/** Set by a world-container bind to re-post a grant that arrived before the bind. */
constexpr std::size_t kGrantDirtyOffset = 393880;
/**
 * Start of the host-state tail msg 12 writes after its 64 region records, header-relative.
 * The tail holds the spawn byte and state, then the teleport state, token, slice-set index and
 * name hash. Their field bases are not settled, so the window is dumped raw rather than indexed.
 */
constexpr std::size_t kHostTailOffset = 365064;
/** Bytes of the tail to dump. Reaches past the name hash under either base. */
constexpr std::size_t kHostTailSize = 48;
/** How long after a message a client is still sampled. The bind lands well inside this. */
constexpr std::uint64_t kSampleWindowMs = 30'000;
/** Sampling cadence. The bind is a tick, not a timer, so this only has to beat the wait. */
constexpr std::uint64_t kSampleIntervalMs = 2'000;
/** Clients the probe tracks at once. One private and one public target is the live shape. */
constexpr std::size_t kTrackedCapacity = 4;

using Receive = char(__fastcall*)(const std::byte*, std::int64_t, int);

/** One ActivityClient seen carrying a membership body, sampled until its window closes. */
struct Tracked {
    const std::byte* client{};
    std::uint64_t expiresAt{};
    std::uint64_t nextSample{};
    bool occupied{};
};

/**
 * Reads one field out of the client.
 * @param client ActivityClient.
 * @param offset Byte offset of the field.
 * @return The field's value.
 */
template <typename T> [[nodiscard]] T field(const std::byte* client, std::size_t offset) noexcept {
    return *reinterpret_cast<const T*>(client + offset);
}

/** @param client ActivityClient. @return Its address, for a log line. */
[[nodiscard]] unsigned long long address_of(const std::byte* client) noexcept {
    return static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(client));
}

hooking::detour::Handle g_handle{};
std::atomic_bool g_installed{false};
/** The detour runs on the client's network thread and the sampler on the callback pump. */
SRWLOCK g_lock{SRWLOCK_INIT};
std::array<Tracked, kTrackedCapacity> g_tracked{};

/**
 * Reports one msg 12 the client actually decoded.
 * @param client ActivityClient the handler was called on.
 * @param before Status word before the call.
 * @param after Status word after it.
 */
void report(const std::byte* client, std::uint16_t before, std::uint16_t after) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const auto memberKey = field<std::uint64_t>(client, kMembershipHeaderOffset);
    const auto axisOne = field<std::uint32_t>(client, kSlotAxisOneOffset);
    const auto axisTwo = field<std::uint32_t>(client, kSlotAxisTwoOffset);
    const auto* label = reinterpret_cast<const char*>(client + kSlotLabelOffset);
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=probe stage=msg12 result=received client=0x%llX "
                                      "slot=%u label=%.*s member=0x%016llX "
                                      "status=0x%04X->0x%04X flag=%u",
                                      address_of(client),
                                      axisOne + 2U * axisTwo,
                                      static_cast<int>(kSlotLabelCapacity),
                                      label,
                                      static_cast<unsigned long long>(memberKey),
                                      static_cast<unsigned>(before),
                                      static_cast<unsigned>(after),
                                      (after & kMembershipFlag) != 0 ? 1U : 0U);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Reports the four bind inputs the rebind reads, none of which needs a call.
 * A zero established id skips the slot silently. A null roster container keeps the slot out of
 * the public-first pick. Between them they name which reader can ever see this client.
 * @param client ActivityClient.
 */
void report_bind_inputs(const std::byte* client) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const auto established = field<std::uint64_t>(client, kEstablishedSessionOffset);
    const auto slotRecord = field<std::uint64_t>(client, kSlotRecordOffset);
    const auto rosterContainer = field<std::uint64_t>(client, kRosterContainerOffset);
    const auto receipt = field<std::uint8_t>(client, kBindReceiptOffset);
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=probe stage=bind client=0x%llX established=0x%016llX "
                                      "slotrec=0x%llX roster=0x%llX receipt=%u",
                                      address_of(client),
                                      static_cast<unsigned long long>(established),
                                      static_cast<unsigned long long>(slotRecord),
                                      static_cast<unsigned long long>(rosterContainer),
                                      static_cast<unsigned>(receipt));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** Bytes per hex log line. Two characters a byte keeps a line well inside its capacity. */
constexpr std::size_t kHexBytesPerLine = 64;
/** Bytes at the membership header to dump, which is where the decoded member table starts. */
constexpr std::size_t kMemberDumpBytes = 512;
/** Most of one region record to dump. A record carrying a 128-byte descriptor still fits. */
constexpr std::size_t kRegionDumpBytes = 320;
/** The wire numbers regions 0, 8, 16 ... 504, so consecutive terms differ by this. */
constexpr std::int32_t kRegionIndexStride = 8;
/** Terms matched before a candidate is accepted as the region table. */
constexpr std::size_t kRegionMatchTerms = 4;
/** Largest in-memory stride between two region records that is still worth testing. */
constexpr std::size_t kMaximumRegionStride = 8'192;
/** End of the searchable span. The entity-slot mask sits above it and holds no region table. */
constexpr std::size_t kScanEndOffset = kPendingMaskOffset;
/** Clients whose region table is dumped. Later messages repeat a table that has already been read.
 */
constexpr std::uint32_t kRegionDumpBudget = 3;

std::atomic<std::uint32_t> g_regionDumps{0};

/**
 * Emits one labelled hex run over as many lines as it needs.
 * @param stage Log stage name.
 * @param base Offset the run starts at, so a line names where its bytes came from.
 * @param data First byte of the run.
 * @param size Bytes to emit.
 */
void report_hex(const char* stage,
                std::size_t base,
                const std::byte* data,
                std::size_t size) noexcept {
    for (std::size_t offset = 0; offset < size; offset += kHexBytesPerLine) {
        std::array<char, core::log::kLineCapacity> line{};
        const std::size_t run =
            (size - offset) < kHexBytesPerLine ? size - offset : kHexBytesPerLine;
        int written = std::snprintf(
            line.data(), line.size(), "ev=probe stage=%s at=%zu raw=", stage, base + offset);
        for (std::size_t index = 0; written > 0 && index < run; ++index) {
            const int part =
                std::snprintf(line.data() + written,
                              line.size() - static_cast<std::size_t>(written),
                              "%02X",
                              std::to_integer<unsigned>(data[offset + index]));
            if (part <= 0) {
                break;
            }
            written += part;
        }
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
}

/**
 * Reports one found region table and the two records that differ by the advertisement.
 * @param client ActivityClient.
 * @param offset Offset of the record holding region 8.
 * @param stride Bytes between two consecutive records.
 */
void report_region_table(const std::byte* client, std::size_t offset, std::size_t stride) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=probe stage=regions result=found client=0x%llX "
                                      "at=%zu stride=%zu",
                                      address_of(client),
                                      offset,
                                      stride);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    // Region 8 is the one the advertisement rides in and region 16 never carries one, so the
    // bytes that differ between them are exactly what the client kept of the descriptor.
    const std::size_t dump = stride < kRegionDumpBytes ? stride : kRegionDumpBytes;
    report_hex("members", kMembershipHeaderOffset, client + kMembershipHeaderOffset, kMemberDumpBytes);
    report_hex("region8", offset, client + offset, dump);
    if (offset + stride + dump <= kScanEndOffset) {
        report_hex("region16", offset + stride, client + offset + stride, dump);
    }
}

/**
 * Finds the client's decoded region table with no signature and dumps two of its records.
 * Four consecutive terms of the 8-step region sequence at one fixed stride name the table, and
 * nothing else in the object is expected to hold that run.
 * @param client ActivityClient the handler has just committed a body into.
 */
void report_regions(const std::byte* client) noexcept {
    if (g_regionDumps.fetch_add(1, std::memory_order_relaxed) >= kRegionDumpBudget) {
        return;
    }
    for (std::size_t offset = kMembershipHeaderOffset; offset + sizeof(std::int32_t) <= kScanEndOffset;
         offset += sizeof(std::int32_t)) {
        if (field<std::int32_t>(client, offset) != kRegionIndexStride) {
            continue;
        }
        for (std::size_t stride = sizeof(std::int32_t); stride <= kMaximumRegionStride;
             stride += sizeof(std::int32_t)) {
            if (offset + kRegionMatchTerms * stride > kScanEndOffset) {
                break;
            }
            bool matched = true;
            for (std::size_t term = 1; matched && term < kRegionMatchTerms; ++term) {
                matched = field<std::int32_t>(client, offset + term * stride)
                          == kRegionIndexStride * static_cast<std::int32_t>(term + 1);
            }
            if (!matched) {
                continue;
            }
            report_region_table(client, offset, stride);
            return;
        }
    }
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=probe stage=regions result=absent");
}

/** @param client ActivityClient. @return Entity-slot bits it holds but has not applied. */
[[nodiscard]] std::size_t pending_slots(const std::byte* client) noexcept {
    const auto* mask = reinterpret_cast<const std::uint8_t*>(client + kPendingMaskOffset);
    std::size_t count = 0;
    for (std::size_t index = 0; index < kPendingMaskSize; ++index) {
        count += static_cast<std::size_t>(std::popcount(mask[index]));
    }
    return count;
}

/** Opens or refreshes the sampling window for one client. */
void track(const std::byte* client, std::uint64_t now) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    Tracked* free = nullptr;
    for (Tracked& entry : g_tracked) {
        if (entry.occupied && entry.client == client) {
            entry.expiresAt = now + kSampleWindowMs;
            ReleaseSRWLockExclusive(&g_lock);
            return;
        }
        if (free == nullptr && !entry.occupied) {
            free = &entry;
        }
    }
    if (free != nullptr) {
        *free = {client, now + kSampleWindowMs, now, true};
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Reads the status word, defers to the original, then reads it again. */
char __fastcall receive(const std::byte* client, std::int64_t body, int size) noexcept {
    auto* original = reinterpret_cast<Receive>(g_handle.original);
    if (original == nullptr) {
        return 0;
    }
    if (client == nullptr) {
        return original(client, body, size);
    }
    const auto before = field<std::uint16_t>(client, kStatusWordOffset);
    const char result = original(client, body, size);
    const auto after = field<std::uint16_t>(client, kStatusWordOffset);
    report(client, before, after);
    report_bind_inputs(client);
    report_regions(client);
    track(client, GetTickCount64());
    return result;
}

/**
 * Dumps the host-state tail the client decoded out of msg 12.
 * The host arms a teleport there and the client's four-step machine reads it. Nothing else says
 * whether the arm reached the client at all, and the field bases are not settled.
 * @param client ActivityClient.
 */
void sample_host_tail(const std::byte* client) noexcept {
    const std::byte* const tail =
        client + kMembershipHeaderOffset + static_cast<std::ptrdiff_t>(kHostTailOffset);
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(
        line.data(), line.size(), "ev=probe stage=hosttail client=0x%llX b=", address_of(client));
    for (std::size_t index = 0; index < kHostTailSize && written > 0; ++index) {
        const int step = std::snprintf(line.data() + written,
                                       line.size() - static_cast<std::size_t>(written),
                                       "%02X",
                                       static_cast<unsigned>(std::to_integer<std::uint8_t>(
                                           tail[static_cast<std::ptrdiff_t>(index)])));
        written = step > 0 ? written + step : 0;
    }
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Reports what one client did with its grant after the message.
 * @param client ActivityClient.
 */
void sample(const std::byte* client) noexcept {
    sample_host_tail(client);
    std::array<char, core::log::kLineCapacity> line{};
    const auto status = field<std::uint16_t>(client, kStatusWordOffset);
    const auto dirty = field<std::uint8_t>(client, kGrantDirtyOffset);
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=probe stage=grant client=0x%llX status=0x%04X "
                      "pending=%zu dirty=%u receipt=%u",
                      address_of(client),
                      static_cast<unsigned>(status),
                      pending_slots(client),
                      static_cast<unsigned>(dirty),
                      static_cast<unsigned>(field<std::uint8_t>(client, kBindReceiptOffset)));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** @param reason Short name of the step that failed. @return Always true: a probe never blocks. */
[[nodiscard]] bool fail(const char* reason) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=probe stage=msg12 result=fail reason=%s", reason);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return true;
}

} // namespace

/** Attaches the read-only probe on the client's activity msg 12 handler. */
bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    std::byte* const target = scan_main_image_unique(kReceive, "membership_probe_msg12");
    if (target == nullptr) {
        return fail("target");
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&receive)};
    if (!hooking::detour::install(spec, g_handle)) {
        return fail("attach");
    }
    g_installed.store(true, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=probe stage=msg12 result=installed");
    return true;
}

/** Samples every ActivityClient the probe has seen recently. */
void service(std::uint64_t now) noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        return;
    }
    // Copied under the lock, sampled outside it: a read walks 1024 bytes and must not hold a lock
    // the detour needs on the client's own thread.
    std::array<const std::byte*, kTrackedCapacity> due{};
    std::size_t count = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (Tracked& entry : g_tracked) {
        if (!entry.occupied) {
            continue;
        }
        if (now >= entry.expiresAt) {
            // The window closes before an activity can plausibly tear down, so the pointer is
            // never read once it could be stale.
            entry = {};
            continue;
        }
        if (now >= entry.nextSample) {
            entry.nextSample = now + kSampleIntervalMs;
            due[count] = entry.client;
            ++count;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    for (std::size_t index = 0; index < count; ++index) {
        sample(due[index]);
    }
}

/** Detaches the probe so a later unload cannot leave a detour into unmapped code. */
bool uninstall() noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    const bool detached = hooking::detour::uninstall(g_handle);
    g_installed.store(!detached, std::memory_order_release);
    return detached;
}

} // namespace sunrise::client::hooks::membership_probe
