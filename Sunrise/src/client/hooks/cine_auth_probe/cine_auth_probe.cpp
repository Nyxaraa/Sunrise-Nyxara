/**
 * Read-only probe on the type-6 cinematic Auth chain: the armed gate, the deferred retry, the
 * body copy, and the start. The start gates refuse silently, so each detour logs the compared
 * values and the outcome, one line per transition. Every detour calls through unchanged and
 * only records; nothing in the game is written.
 */

#include "cine_auth_probe.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"

namespace sunrise::client::hooks::cine_auth_probe {
namespace {

using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/** The Auth entry point. While a destination transfer is armed it returns before the copy. */
constexpr std::string_view kApplyGateText =
    "48 89 5C 24 ? 57 48 83 EC 30 48 8B DA 48 8B F9 E8 ? ? ? ? 84 C0 75 ? 48 8B 05 ? ? ? ? 48 8D "
    "4C 24 ? 4C 8B 4B";
constexpr auto kApplyGate = signature<signature_length(kApplyGateText)>(kApplyGateText);

/** The body copy: 224 bytes to component `+384`, then the generation and start gates. */
constexpr std::string_view kBodyApplyText =
    "40 53 48 83 EC 20 0F 10 02 44 8B 81 90 01 00 00 48 8D 81 80 01 00 00 48 8B D9 0F 11 00";
constexpr auto kBodyApply = signature<signature_length(kBodyApplyText)>(kBodyApplyText);

/** The per-frame update. Once the armed gate opens it re-applies the retained Auth body. */
constexpr std::string_view kUpdateText =
    "40 53 48 83 EC 50 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B D9 E8 ? ? ? ? 84 C0 0F 85 "
    "? ? ? ? 48 89 74 24 ? 48 89 7C 24 ? 38";
constexpr auto kUpdate = signature<signature_length(kUpdateText)>(kUpdateText);

/** The start: writes the participant list, starts by content id, posts the outcome event. */
constexpr std::string_view kStartText =
    "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 44 8B 01 48 8B F9 48 8B 1D ? ? ? ? 41 8B C0 C1 "
    "F8 0D 41 81 E0 FF 1F 00 00";
constexpr auto kStart = signature<signature_length(kStartText)>(kStartText);

/** The armed predicate: an active travel-cinematic object. Called directly, never detoured. */
constexpr std::string_view kArmedText =
    "48 83 EC 28 E8 ? ? ? ? 48 8D 54 24 ? 48 8D 88 00 02 00 00 E8 ? ? ? ? 83 38 FF 0F 95 C0 48 "
    "83 C4 28 C3";
constexpr auto kArmed = signature<signature_length(kArmedText)>(kArmedText);

/** Auth body generation; the copy latches it at component `+400`. */
constexpr std::size_t kBodyGenerationOffset = 16;
/** Playing flag; must differ from the started latch and be 1 to start. */
constexpr std::size_t kBodyPlayingOffset = 20;
/** Teardown flag. */
constexpr std::size_t kBodyTeardownOffset = 21;
/** Target reference key; when set it must resolve to the local player's object. */
constexpr std::size_t kBodyTargetOffset = 24;
/** Player index byte; must be -1 or match the local player. */
constexpr std::size_t kBodyPlayerIndexOffset = 32;
/** Count field; a value of 16 or more refuses the start. */
constexpr std::size_t kBodyCountOffset = 36;
/** Participant id count. */
constexpr std::size_t kBodyParticipantCountOffset = 168;
/** First participant id. */
constexpr std::size_t kBodyParticipantIdsOffset = 172;
/** Latched Auth generation in the component. */
constexpr std::size_t kCompGenerationOffset = 400;
/** Started latch: the last start result. */
constexpr std::size_t kCompStartedOffset = 608;
/** Deferred flag: an Auth body arrived while the armed gate was closed. */
constexpr std::size_t kCompDeferredOffset = 609;
/** Bounded per-window logging survives a long mission without exhausting the intro budget. */
constexpr unsigned kMaxReports = 32;
constexpr ULONGLONG kReportWindowMs = 5000;

using ApplyGate = char(__fastcall*)(void*, void*);
using BodyApply = char(__fastcall*)(void*, const std::byte*);
using UpdateTick = char(__fastcall*)(void*);
using StartCinematic = std::int64_t(__fastcall*)(void*);
using ArmedCheck = bool(__fastcall*)();

/** Detour slots, one per target above. */
enum Index : std::size_t {
    kIdxApplyGate,
    kIdxBodyApply,
    kIdxUpdate,
    kIdxStart,
    kIdxCount,
};

/** Last update-tick state per component; a change is what earns a log line. */
struct UpdateState {
    const void* component{};
    int armed{-1};
    int deferred{-1};
    int latch{-1};
};

std::array<hooking::detour::Handle, kIdxCount> g_handles{};
std::atomic_bool g_installed{false};
ArmedCheck g_armedCheck{nullptr};
// The whole chain runs on the component's owning thread; the counters and flags are diagnostic
// only, so they need no lock.
std::array<UpdateState, 16> g_lastUpdates{};
std::size_t g_nextUpdate{};
ULONGLONG g_reportWindow{};
unsigned g_reports{0};
thread_local bool t_inGate{false};
thread_local bool t_inUpdate{false};
thread_local bool t_startRan{false};

/** Formats a bounded info-level probe line; normal playtest logs retain it. */
void write_line(const char* format, ...) noexcept {
    const auto now = GetTickCount64();
    if (now - g_reportWindow >= kReportWindowMs) {
        g_reportWindow = now;
        g_reports = 0;
    }
    if (g_reports >= kMaxReports) {
        return;
    }
    ++g_reports;
    std::array<char, core::log::kLineCapacity> line{};
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(line.data(), line.size(), format, args);
    va_end(args);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Names the first refusing gate, in the order the applier checks them.
 * @param startRan Whether the start ran inside this apply.
 */
[[nodiscard]] const char* gate_token(std::uint32_t generationOld,
                                     std::uint32_t generation,
                                     int playing,
                                     int latchOld,
                                     int count,
                                     bool startRan) noexcept {
    if (generation == generationOld) {
        return "generation";
    }
    if (playing == latchOld) {
        return "latch";
    }
    if (playing == 0) {
        return "stop";
    }
    if (startRan) {
        return "none";
    }
    // The target and player-index checks run before the count check; a count refusal is
    // decidable from the body alone, a target refusal is what remains.
    return count >= 16 ? "count" : "target";
}

/** Records the armed gate in front of the Auth apply. Armed means nothing is latched. */
char __fastcall apply_gate(void* component, void* reference) noexcept {
    auto* original = reinterpret_cast<ApplyGate>(g_handles[kIdxApplyGate].original);
    if (original == nullptr) {
        return 0;
    }
    const int armed = g_armedCheck != nullptr ? (g_armedCheck() ? 1 : 0) : -1;
    t_inGate = true;
    const char result = original(component, reference);
    t_inGate = false;
    write_line("ev=probe stage=cine6 at=apply armed=%d ret=%d comp=0x%llX",
               armed,
               static_cast<int>(result),
               static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(component)));
    return result;
}

/** Records the body copy, the values every silent start gate compares, and its outcome. */
char __fastcall body_apply(void* component, const std::byte* body) noexcept {
    auto* original = reinterpret_cast<BodyApply>(g_handles[kIdxBodyApply].original);
    if (original == nullptr) {
        return 0;
    }
    const auto* comp = static_cast<const std::byte*>(component);
    std::uint32_t generationOld = 0;
    int latchOld = -1;
    if (comp != nullptr) {
        generationOld = *reinterpret_cast<const std::uint32_t*>(comp + kCompGenerationOffset);
        latchOld = static_cast<int>(comp[kCompStartedOffset]);
    }
    std::uint32_t generation = 0;
    int playing = -1;
    int teardown = -1;
    std::uint32_t target = 0;
    int playerIndex = 0;
    int count = -1;
    int participants = -1;
    unsigned long long firstId = 0;
    unsigned long long secondId = 0;
    if (body != nullptr) {
        generation = *reinterpret_cast<const std::uint32_t*>(body + kBodyGenerationOffset);
        playing = static_cast<int>(body[kBodyPlayingOffset]);
        teardown = static_cast<int>(body[kBodyTeardownOffset]);
        target = *reinterpret_cast<const std::uint32_t*>(body + kBodyTargetOffset);
        playerIndex = static_cast<int>(static_cast<signed char>(body[kBodyPlayerIndexOffset]));
        count = *reinterpret_cast<const int*>(body + kBodyCountOffset);
        participants = *reinterpret_cast<const int*>(body + kBodyParticipantCountOffset);
        if (participants > 0) {
            firstId =
                *reinterpret_cast<const unsigned long long*>(body + kBodyParticipantIdsOffset);
        }
        if (participants > 1) {
            secondId = *reinterpret_cast<const unsigned long long*>(body + kBodyParticipantIdsOffset
                                                                    + sizeof(unsigned long long));
        }
    }
    const bool startBefore = t_startRan;
    t_startRan = false;
    const char result = original(component, body);
    const bool startRan = t_startRan;
    t_startRan = startBefore;
    const int latchNew = comp != nullptr ? static_cast<int>(comp[kCompStartedOffset]) : -1;
    write_line("ev=probe stage=cine6 at=body src=%s gen_old=%u gen=%u playing=%d latch=%d "
               "teardown=%d target=0x%08X pidx=%d count=%d parts=%d id0=0x%llX id1=0x%llX "
               "gate=%s latch_new=%d ret=%d",
               t_inUpdate ? "retry" : (t_inGate ? "push" : "other"),
               generationOld,
               generation,
               playing,
               latchOld,
               teardown,
               target,
               playerIndex,
               count,
               participants,
               firstId,
               secondId,
               gate_token(generationOld, generation, playing, latchOld, count, startRan),
               latchNew,
               static_cast<int>(result));
    return result;
}

/** Records the per-frame update only when armed, deferred, or the start latch changes. */
char __fastcall update_tick(void* component) noexcept {
    auto* original = reinterpret_cast<UpdateTick>(g_handles[kIdxUpdate].original);
    if (original == nullptr) {
        return 0;
    }
    const int armed = g_armedCheck != nullptr ? (g_armedCheck() ? 1 : 0) : -1;
    const auto* comp = static_cast<const std::byte*>(component);
    UpdateState state{};
    state.component = component;
    state.armed = armed;
    // The deferred flag is read at entry: the tick that retries clears it again on its way out.
    if (comp != nullptr) {
        state.deferred = static_cast<int>(comp[kCompDeferredOffset]);
    }
    t_inUpdate = true;
    const char result = original(component);
    t_inUpdate = false;
    if (comp != nullptr) {
        state.latch = static_cast<int>(comp[kCompStartedOffset]);
    }
    UpdateState* previous = nullptr;
    for (auto& entry : g_lastUpdates) {
        if (entry.component == component) { previous = &entry; break; }
    }
    if (previous == nullptr) {
        previous = &g_lastUpdates[g_nextUpdate++ % g_lastUpdates.size()];
        *previous = {};
    }
    auto& g_lastUpdate = *previous;
    if (state.component != g_lastUpdate.component || state.armed != g_lastUpdate.armed
        || state.deferred != g_lastUpdate.deferred || state.latch != g_lastUpdate.latch) {
        g_lastUpdate = state;
        write_line("ev=probe stage=cine6 at=update armed=%d deferred=%d latch=%d comp=0x%llX",
                   state.armed,
                   state.deferred,
                   state.latch,
                   static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(component)));
    }
    return result;
}

/** Records the start outcome: the value the started latch takes and the posted event side. */
std::int64_t __fastcall start_cinematic(void* component) noexcept {
    auto* original = reinterpret_cast<StartCinematic>(g_handles[kIdxStart].original);
    if (original == nullptr) {
        return 0;
    }
    t_startRan = true;
    const std::int64_t result = original(component);
    write_line("ev=probe stage=cine6 at=start ok=%lld comp=0x%llX",
               static_cast<long long>(result),
               static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(component)));
    return result;
}

/** @param reason Short name of the step that failed. @return Always false. */
[[nodiscard]] bool fail(const char* reason) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=probe stage=cine6 result=fail reason=%s", reason);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return false;
}

} // namespace

/** Attaches all four read-only detours in one transaction, or none of them. */
bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    std::byte* const armed = scan_main_image_unique(kArmed, "cine_auth_probe_armed");
    if (armed == nullptr) {
        return fail("cine_auth_probe_armed");
    }
    g_armedCheck = reinterpret_cast<ArmedCheck>(armed);
    struct Target {
        std::span<const patterns::PatternByte> pattern;
        const char* name;
        void* replacement;
    };
    const std::array<Target, kIdxCount> targets{{
        {kApplyGate, "cine_auth_probe_apply", reinterpret_cast<void*>(&apply_gate)},
        {kBodyApply, "cine_auth_probe_body", reinterpret_cast<void*>(&body_apply)},
        {kUpdate, "cine_auth_probe_update", reinterpret_cast<void*>(&update_tick)},
        {kStart, "cine_auth_probe_start", reinterpret_cast<void*>(&start_cinematic)},
    }};
    std::array<hooking::detour::Spec, kIdxCount> specs{};
    for (std::size_t index = 0; index < kIdxCount; ++index) {
        std::byte* const found =
            scan_main_image_unique(targets[index].pattern, targets[index].name);
        if (found == nullptr) {
            return fail(targets[index].name);
        }
        specs[index] = {found, targets[index].replacement};
    }
    if (!hooking::detour::install(specs, g_handles)) {
        return fail("attach");
    }
    g_installed.store(true, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=probe stage=cine6 result=installed");
    return true;
}

/** Detaches every probe detour in one transaction. */
bool uninstall() noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    const bool detached = hooking::detour::uninstall(g_handles);
    g_installed.store(!detached, std::memory_order_release);
    return detached;
}

} // namespace sunrise::client::hooks::cine_auth_probe
