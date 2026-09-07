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
 * `ActivityMsg12_ReplicateMembership_Recv`. Its first argument is the ActivityClient.
 * It commits the membership block, then sets the status bit unconditionally and returns 1.
 */
constexpr std::string_view kReceiveText = "40 55 53 41 56 41 57 48 8D AC 24 ? ? ? ? B8 C8 96 05 00";
constexpr auto kReceive = signature<signature_length(kReceiveText)>(kReceiveText);

/** Status word the handler writes, one bit per membership step. */
constexpr std::size_t kStatusWordOffset = 304;
/** Membership header. Its leading qword is the member key the client matches itself by. */
constexpr std::size_t kMembershipHeaderOffset = 27696;
/**
 * The two slot axes and the label built from them, indexed as `axis1 + 2 * axis2`.
 * Axis 1 is PRIVATE or PUBLIC, axis 2 is CURRENT or TARGET. Index 2 or 3 is a TARGET slot.
 */
constexpr std::size_t kSlotAxisOneOffset = 24;
constexpr std::size_t kSlotAxisTwoOffset = 28;
constexpr std::size_t kSlotLabelOffset = 32;
/** The label is NUL-terminated; "PRIVATE CURRENT" is the longest form. */
constexpr std::size_t kSlotLabelCapacity = 32;
/**
 * Established session id, returned by the client's own vtable slot 1.
 * Zero makes the rebind skip the slot with no log. Msg 4's accept arm is its only writer.
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
 * Sticky bind receipt: one the moment a world container binds, zero again only on session reset.
 * The grant dirty byte clears within a tick, so a sample can miss that one but never this.
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
 * Field bases inside the tail are not settled, so the window is dumped raw rather than indexed.
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
 * Reports the four bind inputs the rebind reads.
 * A zero established id skips the slot; a null roster container keeps it out of the public pick.
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
    track(client, GetTickCount64());
    return result;
}

/**
 * Dumps the host-state tail the client decoded out of msg 12.
 * The field bases inside it are not settled, so the window is dumped raw.
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
    if (detached) {
        // Clear the tracked clients so a later install cannot sample a torn-down pointer.
        AcquireSRWLockExclusive(&g_lock);
        g_tracked = {};
        ReleaseSRWLockExclusive(&g_lock);
    }
    return detached;
}

} // namespace sunrise::client::hooks::membership_probe
