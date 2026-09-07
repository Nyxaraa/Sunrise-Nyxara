/**
 * Probe on the prologue-filler boot task's cinematic readiness chain.
 * While the task runs, this logs once per second which readiness stage refused, plus a
 * freeze-discriminator line: ticks, gate arms, clock vs duration, heal counters, request drops.
 * Every detour calls through unchanged and only records.
 */

#include "cine_probe.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"

namespace sunrise::client::hooks::cine_probe {
namespace {

using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/** The prologue-filler entry task update. Runs once per frame while the task is pending. */
constexpr std::string_view kTaskText =
    "48 89 5C 24 ? 48 89 7C 24 ? 55 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 "
    "C4 48 89 85 ? ? ? ? 48 8D 79 30 83";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kTask = signature<signature_length(kTaskText)>(kTaskText);

/** The start-latch read, `return cine[0x269]`. Its argument is the cinematic block. */
constexpr std::string_view kStartedText = "0F B6 81 69 02 00 00 C3";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kStarted = signature<signature_length(kStartedText)>(kStartedText);

/** The no-argument component-present check on the active cinematic object at manager `+512`. */
constexpr std::string_view kComponentText =
    "48 83 EC ? E8 ? ? ? ? 48 8D 54 24 ? 48 8D 88 00 02 00 00 E8 ? ? ? ? 8B 44 24 ? 83 F8 FF 74 "
    "? 48 8B 15";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kComponent = signature<signature_length(kComponentText)>(kComponentText);

/** The final readiness predicate: component, then each participant, then the nested tag. */
constexpr std::string_view kFinalText =
    "40 55 56 41 56 48 8D 6C 24 ? 48 81 EC ? ? ? ? 4C 8B F2 48 8B F1 E8 ? ? ? ? 48 8D 55 ? 48 8D "
    "88 00 02 00 00 E8";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kFinal = signature<signature_length(kFinalText)>(kFinalText);

/** The per-participant readiness test, called with the participant kind and the asked tag. */
constexpr std::string_view kParticipantText =
    "48 89 5C 24 ? 55 48 8B EC 48 81 EC ? ? ? ? 0F 10 01 49 8B D8 44 8B C2 0F 10 49 10";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kParticipant = signature<signature_length(kParticipantText)>(kParticipantText);

/** The tag test, a virtual at interface `+0xD8`. Both readiness predicates end in it. */
constexpr std::string_view kTagTestText =
    "48 83 EC 28 4C 8B 01 48 8B 49 08 49 8B 40 18 4D 8B 8C 00 D8 00 00 00 8B 02 48 8D 54 24 30 "
    "89 44 24 30 41 FF D1 48 83 C4 28 C3";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kTagTest = signature<signature_length(kTagTestText)>(kTagTestText);

/** The current-state getter, a virtual at interface `+0xC0`. Called directly, never detoured. */
constexpr std::string_view kStateGetterText =
    "40 53 48 83 EC 20 C7 02 C5 9D 1C 81 48 8B DA 4C 8B 01 48 8D 54 24 ? 48 8B 49 08 49 8B 40 18 "
    "4D 8B 8C 00 C0 00 00 00 41 FF D1";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kStateGetter = signature<signature_length(kStateGetterText)>(kStateGetterText);

/** The sequence-state request. It refuses without any log when no definition is bound. */
constexpr std::string_view kSeqRequestText =
    "4C 8B DC 56 41 56 41 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 4D 8B F0 "
    "4D 8B F9 44 8B 41 10 48 8B F1 41 83 F8 FF";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kSeqRequest = signature<signature_length(kSeqRequestText)>(kSeqRequestText);

/** The cine-block mode-advance tick, dispatched once per frame by the object-message system. */
constexpr std::string_view kTickText =
    "48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 54 41 56 41 57 48 83 EC 20 44 8B 01 48 8B F1 41 8B C0 "
    "41 81 E0 FF 1F 00 00 C1 F8 0D 8B D0 4C 8B 76 08";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kTick = signature<signature_length(kTickText)>(kTickText);

/** The time-gated variant pick, arm 2 of the mode-advance gate. Its second argument is the mode. */
constexpr std::string_view kPickText =
    "48 8B C4 41 54 41 55 41 57 48 81 EC ? ? ? ? 4C 63 E2 45 32 ED 4C 8B F9 41 83 FC FF 0F 84 ? "
    "? ? ? 48 89 58 08";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kPick = signature<signature_length(kPickText)>(kPickText);

/** The sequence-object alive test, arm 1 of the mode-advance gate. */
constexpr std::string_view kAliveText =
    "83 B9 E0 00 00 00 00 7F 09 83 B9 F8 00 00 00 00 7E 0C F6 81 46 02 00 00 10 75 03 B0 01 C3 "
    "32 C0 C3";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kAlive = signature<signature_length(kAliveText)>(kAliveText);

/** The playback-clock read the variant pick compares against the duration. */
constexpr std::string_view kClockText =
    "48 83 EC 28 44 8B 01 4C 8B C9 41 8B C0 41 81 E0 FF 1F 00 00 C1 F8 0D 8B D0 48 81 CA 00 00 "
    "FC 0F 0F B7 C0 48 C1 EA 12 48 23 D0 48 8B 05 ? ? ? ? 48 C1 E2 06 48 03 10 44 0F AF 42 30 48 "
    "63 4A 34 41 8B C0 48 03 42 08 49 8B 51 08 48 23 48 08 48 2B C1 44 0F B6 84 10 D0 02 00 00 "
    "41 80 F8 FF 0F 84 ? ? ? ? 45 84 C0";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kClock = signature<signature_length(kClockText)>(kClockText);

/** The duration read. Same body as the clock read until the final branch, so the tail decides. */
constexpr std::string_view kDurationText =
    "44 8B 01 4C 8B C9 41 8B C0 41 81 E0 FF 1F 00 00 C1 F8 0D 8B D0 48 81 CA 00 00 FC 0F 0F B7 "
    "C0 48 C1 EA 12 48 23 D0 48 8B 05 ? ? ? ? 48 C1 E2 06 48 03 10 44 0F AF 42 30 48 63 4A 34 41 "
    "8B C0 48 03 42 08 49 8B 51 08 48 23 48 08 48 2B C1 44 0F B6 84 10 D0 02 00 00 41 80 F8 FF "
    "74 ? 45 84 C0 75";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kDuration = signature<signature_length(kDurationText)>(kDurationText);

/** The mode-start heal: re-creates every entry of the mode whose instance no longer resolves. */
constexpr std::string_view kHealText =
    "40 57 41 54 41 56 41 57 48 83 EC 38 44 8B 11 4C 8B F9 48 8B 49 08 45 8B C2 48 8B 05 ? ? ? "
    "? 41 81 E2 FF 1F 00 00 41 C1 F8 0D";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kHeal = signature<signature_length(kHealText)>(kHealText);

/** The rig bind at attach. Writes both player handles; a false return is permanent. */
constexpr std::string_view kBindText =
    "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC 20 41 8B D9 41 8B F0 8B FA 4C 8B "
    "F1 E8 ? ? ? ? 8B 44 24 ? 41 89 86 A0 00 00 00 41 89 76 14 41 89 5E 04";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kBind = signature<signature_length(kBindText)>(kBindText);

/** The sticky wipe on a failed state bind: target and current state both go to -1. */
constexpr std::string_view kResetText =
    "40 53 48 83 EC 20 48 C7 81 A4 00 00 00 FF FF FF FF 48 8B D9 48 C7 81 AC 00 00 00 FF FF FF "
    "FF C7 81 B4 00 00 00 FF FF FF FF C7 81 B8 00 00 00 C5 9D 1C 81";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kReset = signature<signature_length(kResetText)>(kResetText);

/** The fresh-instance create the heal calls once per entry it re-creates. */
constexpr std::string_view kCreateText =
    "48 89 6C 24 ? 56 57 41 54 41 56 41 57 48 81 EC E0 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 "
    "84 24 ? ? ? ? 44 8B 01 4C 8B F1 41 8B C0";
/** Compiled form of the pattern above; the scan requires one match. */
constexpr auto kCreate = signature<signature_length(kCreateText)>(kCreateText);

/** Primary local-player object slot in the cinematic block. */
constexpr std::size_t kPrimaryObjectOffset = 104;
/** Count of the extra local-player objects in the cinematic block. */
constexpr std::size_t kExtraCountOffset = 112;
/** Activation stage byte in the cinematic block. */
constexpr std::size_t kBlockActivationOffset = 49;
/** Hold byte in the cinematic block; while set, only the countdown runs. */
constexpr std::size_t kBlockHoldOffset = 50;
/** Hold countdown in the cinematic block, in ticks. */
constexpr std::size_t kBlockCountdownOffset = 52;
/** Current mode index in the cinematic block; -1 means the machine is stopped. */
constexpr std::size_t kBlockModeOffset = 56;
/** Sequence-definition handle in the sequence player; -1 means no definition is bound. */
constexpr std::size_t kSeqDefinitionOffset = 16;
/** Seq-host component handle in the sequence player; -1 means the rig had no component. */
constexpr std::size_t kSeqComponentOffset = 20;
/** Target state index the sequence-state request writes into the sequence player. */
constexpr std::size_t kSeqTargetOffset = 164;
/** One summary line per second while the task update keeps running. */
constexpr std::uint64_t kReportIntervalMs = 1'000;
/** Process-wide line cap, so a stuck boot cannot flood the log. */
constexpr unsigned kMaxReports = 256;

using TaskUpdate = std::int64_t(__fastcall*)(void*);
using StartedRead = std::int64_t(__fastcall*)(const std::byte*);
using ComponentCheck = bool(__fastcall*)();
using FinalCheck = char(__fastcall*)(const std::byte*, int*);
using ParticipantCheck = char(__fastcall*)(void*, std::uint64_t, int*);
using TagCheck = std::int64_t(__fastcall*)(void*, int*);
using StateTagGetter = std::uint32_t*(__fastcall*)(void*, std::uint32_t*);
using SeqRequest = char(__fastcall*)(void*, std::uint32_t*, std::uint32_t*, void*);
// A decompiler argument count is an inference, so the freeze detours below forward all four
// integer argument registers. None of the targets takes a float argument.
using TickAdvance = char(__fastcall*)(std::byte*, std::uint64_t, std::uint64_t, std::uint64_t);
using PickVariant = std::int64_t(__fastcall*)(void*, std::uint64_t, std::uint64_t, std::uint64_t);
using AliveCheck = bool(__fastcall*)(void*, std::uint64_t, std::uint64_t, std::uint64_t);
using FloatRead = float(__fastcall*)(void*, std::uint64_t, std::uint64_t, std::uint64_t);
using ModeHeal = char(__fastcall*)(void*, std::uint64_t, std::uint64_t, std::uint64_t);
using EntryCreate = std::int64_t(__fastcall*)(void*, std::uint64_t, std::uint64_t, std::uint64_t);
// The bind takes seven int arguments, three of them on the stack; all are forwarded.
using RigBind = bool(__fastcall*)(std::byte*, int, int, int, int, int, int);
using ResetRequest = std::int64_t(__fastcall*)(void*, std::uint64_t, std::uint64_t, std::uint64_t);

/** Detour slots, one per target above. */
enum Index : std::size_t {
    kIdxTask,
    kIdxStarted,
    kIdxComponent,
    kIdxFinal,
    kIdxParticipant,
    kIdxTagTest,
    kIdxSeqRequest,
    kIdxTick,
    kIdxPick,
    kIdxAlive,
    kIdxClock,
    kIdxDuration,
    kIdxHeal,
    kIdxCreate,
    kIdxBind,
    kIdxReset,
    kIdxCount,
};

/** What the inner detours saw during one task update. Tristate ints are -1 until seen. */
struct FrameSample {
    const std::byte* cine{};
    int started{-1};
    int component{-1};
    int finalReady{-1};
    std::uint32_t finalTag{};
    unsigned participantCalls{};
    int participantResult{-1};
    std::uint64_t participantKind{};
    std::uint32_t participantTag{};
    int participantTagTest{-1};
    int finalTagTest{-1};
    std::uint32_t participantCurrentTag{};
};

/** What the last sequence-state request saw, latched for the summary line. */
struct SeqRequestSample {
    const void* player{};
    std::uint32_t tag{};
    int definition{-1};
    int target{-1};
    int result{-1};
};

/** What the mode-machine detours saw during the last tick dispatch. Tristate ints are -1. */
struct TickSample {
    const std::byte* block{};
    int activation{-1};
    int hold{-1};
    int countdown{-1};
    int mode{-1};
    int alive{-1};
    int pick{-1};
    int pickMode{-1};
    float clock{-1.0F};
    float duration{-1.0F};
};

std::array<hooking::detour::Handle, kIdxCount> g_handles{};
std::atomic_bool g_installed{false};
// The task update and everything it calls run on one thread; the flags keep every other caller
// of the shared targets out, so the sample needs no lock.
FrameSample g_sample{};
StateTagGetter g_stateGetter{nullptr};
// Requests arrive from the simulation element apply, not the task update. The latch is
// diagnostic only; a torn read costs one line, never a crash.
SeqRequestSample g_lastRequest{};
// The tick dispatch is not proven to share the task's thread. The counters are atomic; the
// sample is diagnostic only, and a torn read costs one line, never a crash.
TickSample g_tick{};
std::atomic<unsigned> g_tickCalls{0};
std::atomic<unsigned> g_healCalls{0};
std::atomic<unsigned> g_healCreates{0};
std::atomic<int> g_healMode{-1};
// The request's first predicate refuses an unbound player with no log line of its own.
std::atomic<unsigned> g_seqDrops{0};
std::atomic<std::uint32_t> g_dropTag{0};
// The bind and reset detours are not proven to share the task's thread; their caps are atomic.
std::atomic<unsigned> g_bindReports{0};
std::atomic<unsigned> g_resetReports{0};
/** Tick total at the last report; read and written on the task thread only. */
unsigned g_tickCallsSeen{0};
/** Drop total at the last report; read and written on the task thread only. */
unsigned g_seqDropsSeen{0};
std::uint64_t g_nextReportTick{0};
unsigned g_reports{0};
unsigned g_requestReports{0};
thread_local bool t_inTask{false};
thread_local bool t_inFinal{false};
thread_local bool t_inParticipant{false};
thread_local bool t_inStateGetter{false};
thread_local bool t_inTick{false};
thread_local bool t_inPick{false};
thread_local bool t_pickSeen{false};
thread_local bool t_aliveSeen{false};
thread_local bool t_inHeal{false};

/** @return The stage the sample shows as false, as a short log token. */
[[nodiscard]] const char* failing_stage(const FrameSample& sample) noexcept {
    if (sample.finalReady == 1) {
        return "none";
    }
    if (sample.started == 0) {
        return "start";
    }
    if (sample.component == 0) {
        return "component";
    }
    if (sample.finalReady == 0) {
        return sample.participantResult == 0 ? "participant" : "final_tag";
    }
    return "idle";
}

/**
 * Emits the freeze-discriminator line. ticks=0 means the dispatch itself stopped.
 * It carries ticks, silent request drops, both mode-advance gate arms, the clock and duration
 * pair, the heal counters, and whether the task polls the ticked block.
 */
void report_freeze() noexcept {
    const unsigned total = g_tickCalls.load(std::memory_order_relaxed);
    const unsigned ticks = total - g_tickCallsSeen;
    g_tickCallsSeen = total;
    const unsigned dropTotal = g_seqDrops.load(std::memory_order_relaxed);
    const unsigned drops = dropTotal - g_seqDropsSeen;
    g_seqDropsSeen = dropTotal;
    const TickSample sample = g_tick;
    // The task-side detours record the block the task polls the start latch on this frame.
    int taskBlock = -1;
    if (sample.block != nullptr && g_sample.cine != nullptr) {
        taskBlock = sample.block == g_sample.cine ? 1 : 0;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=probe stage=freeze ticks=%u mode=%d act=%d hold=%d wait=%d alive=%d pick=%d "
        "pick_mode=%d clock=%.3f dur=%.3f heal_calls=%u heal_created=%u heal_mode=%d block=0x%llX "
        "task7=%d drops=%u drop_tag=0x%08X",
        ticks,
        sample.mode,
        sample.activation,
        sample.hold,
        sample.countdown,
        sample.alive,
        sample.pick,
        sample.pickMode,
        static_cast<double>(sample.clock),
        static_cast<double>(sample.duration),
        g_healCalls.load(std::memory_order_relaxed),
        g_healCreates.load(std::memory_order_relaxed),
        g_healMode.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(sample.block)),
        taskBlock,
        drops,
        g_dropTag.load(std::memory_order_relaxed));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Emits one summary line per interval, from the sample the update just filled.
 * Runs on the task's own thread, so the cinematic block fields are read where they are owned.
 * @param state The task update's returned state.
 */
void report(std::int64_t state) noexcept {
    const std::uint64_t now = GetTickCount64();
    if (now < g_nextReportTick || g_reports >= kMaxReports) {
        return;
    }
    ++g_reports;
    g_nextReportTick = now + kReportIntervalMs;
    const FrameSample& sample = g_sample;
    unsigned long long primary = 0;
    int extras = -1;
    if (sample.cine != nullptr) {
        primary = *reinterpret_cast<const std::uint64_t*>(sample.cine + kPrimaryObjectOffset);
        extras = *reinterpret_cast<const int*>(sample.cine + kExtraCountOffset);
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=probe stage=cine fail=%s ret=%lld started=%d comp=%d final=%d "
        "tag=0x%08X parts=%u part_res=%d part_kind=%llu part_tag=0x%08X "
        "part_tagtest=%d final_tagtest=%d cur=0x%08X cine=0x%llX primary=0x%llX extras=%d "
        "seq_def=%d seq_target=%d seq_tag=0x%08X",
        failing_stage(sample),
        static_cast<long long>(state),
        sample.started,
        sample.component,
        sample.finalReady,
        sample.finalTag,
        sample.participantCalls,
        sample.participantResult,
        static_cast<unsigned long long>(sample.participantKind),
        sample.participantTag,
        sample.participantTagTest,
        sample.finalTagTest,
        sample.participantCurrentTag,
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(sample.cine)),
        primary,
        extras,
        g_lastRequest.definition,
        g_lastRequest.target,
        g_lastRequest.tag);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    report_freeze();
}

/** Resets the sample, runs the task update under the thread flag, then reports. */
std::int64_t __fastcall task_update(void* task) noexcept {
    auto* original = reinterpret_cast<TaskUpdate>(g_handles[kIdxTask].original);
    if (original == nullptr) {
        return 0;
    }
    g_sample = {};
    t_inTask = true;
    const std::int64_t state = original(task);
    t_inTask = false;
    report(state);
    return state;
}

/** Records the start latch and the cinematic block the task polls it on. */
std::int64_t __fastcall started_read(const std::byte* cine) noexcept {
    auto* original = reinterpret_cast<StartedRead>(g_handles[kIdxStarted].original);
    if (original == nullptr) {
        return 0;
    }
    const std::int64_t started = original(cine);
    if (t_inTask) {
        g_sample.cine = cine;
        g_sample.started = started != 0 ? 1 : 0;
    }
    return started;
}

/** Records whether the active cinematic object still carries the required component. */
bool __fastcall component_check() noexcept {
    auto* original = reinterpret_cast<ComponentCheck>(g_handles[kIdxComponent].original);
    if (original == nullptr) {
        return false;
    }
    const bool present = original();
    if (t_inTask) {
        g_sample.component = present ? 1 : 0;
    }
    return present;
}

/** Records the final readiness result and the tag the task asked it for. */
char __fastcall final_check(const std::byte* cine, int* tag) noexcept {
    auto* original = reinterpret_cast<FinalCheck>(g_handles[kIdxFinal].original);
    if (original == nullptr) {
        return 0;
    }
    const bool record = t_inTask;
    if (record) {
        g_sample.cine = cine;
        g_sample.finalTag = tag != nullptr ? static_cast<std::uint32_t>(*tag) : 0U;
        t_inFinal = true;
    }
    const char result = original(cine, tag);
    if (record) {
        t_inFinal = false;
        g_sample.finalReady = result != 0 ? 1 : 0;
    }
    return result;
}

/** Records each participant test: its kind, the asked tag, and the result. */
char __fastcall participant_check(void* component, std::uint64_t kind, int* tag) noexcept {
    auto* original = reinterpret_cast<ParticipantCheck>(g_handles[kIdxParticipant].original);
    if (original == nullptr) {
        return 0;
    }
    const bool record = t_inTask;
    if (record) {
        ++g_sample.participantCalls;
        g_sample.participantKind = kind;
        g_sample.participantTag = tag != nullptr ? static_cast<std::uint32_t>(*tag) : 0U;
        // Reset per call: a failed test with no tag call means the role map or interface is
        // missing, which accepts only the fallback tag.
        g_sample.participantTagTest = -1;
        t_inParticipant = true;
    }
    const char result = original(component, kind, tag);
    if (record) {
        t_inParticipant = false;
        g_sample.participantResult = result != 0 ? 1 : 0;
    }
    return result;
}

/** Records the tag test outcome, split by which readiness predicate is running it. */
std::int64_t __fastcall tag_check(void* iface, int* tag) noexcept {
    auto* original = reinterpret_cast<TagCheck>(g_handles[kIdxTagTest].original);
    if (original == nullptr) {
        return 0;
    }
    const std::int64_t result = original(iface, tag);
    // The virtual returns in AL; the upper RAX bytes are garbage, so test the low byte only.
    const bool passed = (result & 0xFF) != 0;
    if (t_inParticipant && !t_inStateGetter) {
        g_sample.participantTagTest = passed ? 1 : 0;
        // Same interface pair, sibling getter: the state the sequence is in right now. The
        // guard flag keeps a getter implementation that tests tags from re-entering here.
        if (g_stateGetter != nullptr) {
            std::uint32_t current = 0;
            t_inStateGetter = true;
            g_stateGetter(iface, &current);
            t_inStateGetter = false;
            g_sample.participantCurrentTag = current;
        }
    } else if (t_inFinal) {
        g_sample.finalTagTest = passed ? 1 : 0;
    }
    return result;
}

/**
 * Records each sequence-state request. A refused request returns 0 with no other trace; def=-1
 * on that line means the player had no bound sequence definition.
 */
char __fastcall seq_request(void* player,
                            std::uint32_t* tag,
                            std::uint32_t* fallbackTag,
                            void* position) noexcept {
    auto* original = reinterpret_cast<SeqRequest>(g_handles[kIdxSeqRequest].original);
    if (original == nullptr) {
        return 0;
    }
    const char result = original(player, tag, fallbackTag, position);
    SeqRequestSample sample{};
    sample.player = player;
    sample.tag = tag != nullptr ? *tag : 0U;
    if (player != nullptr) {
        const auto* bytes = static_cast<const std::byte*>(player);
        sample.definition = *reinterpret_cast<const int*>(bytes + kSeqDefinitionOffset);
        sample.target = *reinterpret_cast<const int*>(bytes + kSeqTargetOffset);
    }
    sample.result = result != 0 ? 1 : 0;
    // def=-1 is the request's first predicate: it returned 0 here before touching any state.
    if (player != nullptr && sample.definition == -1) {
        g_seqDrops.fetch_add(1, std::memory_order_relaxed);
        g_dropTag.store(sample.tag, std::memory_order_relaxed);
    }
    const bool changed = sample.player != g_lastRequest.player || sample.tag != g_lastRequest.tag
                         || sample.definition != g_lastRequest.definition
                         || sample.result != g_lastRequest.result;
    g_lastRequest = sample;
    if (changed && g_requestReports < kMaxReports) {
        ++g_requestReports;
        std::array<char, core::log::kLineCapacity> line{};
        const int written =
            std::snprintf(line.data(),
                          line.size(),
                          "ev=probe stage=seq player=0x%llX tag=0x%08X def=%d target=%d ret=%d",
                          static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(player)),
                          sample.tag,
                          sample.definition,
                          sample.target,
                          sample.result);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::debug,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
    return result;
}

/** Counts each mode-machine tick and snapshots the block fields the advance gate reads. */
char __fastcall tick_advance(std::byte* block,
                             std::uint64_t a2,
                             std::uint64_t a3,
                             std::uint64_t a4) noexcept {
    auto* original = reinterpret_cast<TickAdvance>(g_handles[kIdxTick].original);
    if (original == nullptr) {
        return 0;
    }
    g_tickCalls.fetch_add(1, std::memory_order_relaxed);
    g_tick = {};
    if (block != nullptr) {
        g_tick.block = block;
        g_tick.activation = static_cast<int>(
            *reinterpret_cast<const std::uint8_t*>(block + kBlockActivationOffset));
        g_tick.hold =
            static_cast<int>(*reinterpret_cast<const std::uint8_t*>(block + kBlockHoldOffset));
        g_tick.countdown = *reinterpret_cast<const int*>(block + kBlockCountdownOffset);
        g_tick.mode = *reinterpret_cast<const int*>(block + kBlockModeOffset);
    }
    t_inTick = true;
    t_pickSeen = false;
    t_aliveSeen = false;
    const char result = original(block, a2, a3, a4);
    t_inTick = false;
    return result;
}

/** Records the variant pick's mode and result, and opens the clock/duration capture window. */
std::int64_t __fastcall pick_variant(void* block,
                                     std::uint64_t mode,
                                     std::uint64_t a3,
                                     std::uint64_t a4) noexcept {
    auto* original = reinterpret_cast<PickVariant>(g_handles[kIdxPick].original);
    if (original == nullptr) {
        return 0;
    }
    const bool record = t_inTick;
    if (record) {
        g_tick.pickMode = static_cast<int>(static_cast<std::uint32_t>(mode));
        t_inPick = true;
    }
    const std::int64_t result = original(block, mode, a3, a4);
    if (record) {
        t_inPick = false;
        t_pickSeen = true;
        // The pick returns in AL; the upper RAX bytes are garbage, so test the low byte only.
        g_tick.pick = (result & 0xFF) != 0 ? 1 : 0;
    }
    return result;
}

/** Records the gate's alive test: the first alive call in the tick after the pick returned. */
bool __fastcall alive_check(void* object,
                            std::uint64_t a2,
                            std::uint64_t a3,
                            std::uint64_t a4) noexcept {
    auto* original = reinterpret_cast<AliveCheck>(g_handles[kIdxAlive].original);
    if (original == nullptr) {
        return false;
    }
    const bool alive = original(object, a2, a3, a4);
    if (t_inTick && t_pickSeen && !t_aliveSeen) {
        t_aliveSeen = true;
        g_tick.alive = alive ? 1 : 0;
    }
    return alive;
}

/** Records the playback clock the pick reads. Only the pick's own read is captured. */
float __fastcall clock_read(void* definition,
                            std::uint64_t a2,
                            std::uint64_t a3,
                            std::uint64_t a4) noexcept {
    auto* original = reinterpret_cast<FloatRead>(g_handles[kIdxClock].original);
    if (original == nullptr) {
        return 0.0F;
    }
    const float value = original(definition, a2, a3, a4);
    if (t_inPick) {
        g_tick.clock = value;
    }
    return value;
}

/** Records the duration the pick compares the clock against. */
float __fastcall duration_read(void* definition,
                               std::uint64_t a2,
                               std::uint64_t a3,
                               std::uint64_t a4) noexcept {
    auto* original = reinterpret_cast<FloatRead>(g_handles[kIdxDuration].original);
    if (original == nullptr) {
        return 0.0F;
    }
    const float value = original(definition, a2, a3, a4);
    if (t_inPick) {
        g_tick.duration = value;
    }
    return value;
}

/** Counts each mode-start heal call and remembers the mode it healed. */
char __fastcall mode_heal(void* block,
                          std::uint64_t mode,
                          std::uint64_t a3,
                          std::uint64_t a4) noexcept {
    auto* original = reinterpret_cast<ModeHeal>(g_handles[kIdxHeal].original);
    if (original == nullptr) {
        return 0;
    }
    g_healCalls.fetch_add(1, std::memory_order_relaxed);
    g_healMode.store(static_cast<int>(static_cast<std::uint32_t>(mode)), std::memory_order_relaxed);
    t_inHeal = true;
    const char result = original(block, mode, a3, a4);
    t_inHeal = false;
    return result;
}

/** Counts the entries the heal re-created. Only creates inside the heal are counted. */
std::int64_t __fastcall entry_create(void* entry,
                                     std::uint64_t a2,
                                     std::uint64_t a3,
                                     std::uint64_t a4) noexcept {
    auto* original = reinterpret_cast<EntryCreate>(g_handles[kIdxCreate].original);
    if (original == nullptr) {
        return 0;
    }
    const std::int64_t result = original(entry, a2, a3, a4);
    if (t_inHeal) {
        g_healCreates.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

/** Logs each rig bind: the rig argument, the result, and both player handles after the call. */
bool __fastcall rig_bind(
    std::byte* player, int rig, int component, int a4, int a5, int a6, int a7) noexcept {
    auto* original = reinterpret_cast<RigBind>(g_handles[kIdxBind].original);
    if (original == nullptr) {
        return false;
    }
    const bool bound = original(player, rig, component, a4, a5, a6, a7);
    int definition = -1;
    int componentHandle = -1;
    if (player != nullptr) {
        definition = *reinterpret_cast<const int*>(player + kSeqDefinitionOffset);
        componentHandle = *reinterpret_cast<const int*>(player + kSeqComponentOffset);
    }
    if (g_bindReports.load(std::memory_order_relaxed) < kMaxReports) {
        g_bindReports.fetch_add(1, std::memory_order_relaxed);
        std::array<char, core::log::kLineCapacity> line{};
        const int written =
            std::snprintf(line.data(),
                          line.size(),
                          "ev=probe stage=bind player=0x%llX rig=0x%08X ret=%d def=%d comp=%d "
                          "in_task=%d",
                          static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(player)),
                          static_cast<unsigned>(rig),
                          bound ? 1 : 0,
                          definition,
                          componentHandle,
                          t_inTask ? 1 : 0);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::debug,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
    return bound;
}

/** Logs each sticky state-request wipe with the player it hit. */
std::int64_t __fastcall reset_request(void* player,
                                      std::uint64_t a2,
                                      std::uint64_t a3,
                                      std::uint64_t a4) noexcept {
    auto* original = reinterpret_cast<ResetRequest>(g_handles[kIdxReset].original);
    if (original == nullptr) {
        return 0;
    }
    const std::int64_t result = original(player, a2, a3, a4);
    if (g_resetReports.load(std::memory_order_relaxed) < kMaxReports) {
        g_resetReports.fetch_add(1, std::memory_order_relaxed);
        std::array<char, core::log::kLineCapacity> line{};
        const int written =
            std::snprintf(line.data(),
                          line.size(),
                          "ev=probe stage=reset player=0x%llX in_task=%d",
                          static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(player)),
                          t_inTask ? 1 : 0);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::debug,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
    return result;
}

/** @param reason Short name of the step that failed. @return Always false. */
[[nodiscard]] bool fail(const char* reason) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=probe stage=cine result=fail reason=%s", reason);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return false;
}

} // namespace

/** Attaches all sixteen read-only detours in one transaction, or none of them. */
bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    struct Target {
        std::span<const patterns::PatternByte> pattern;
        const char* name;
        void* replacement;
    };
    const std::array<Target, kIdxCount> targets{{
        {kTask, "cine_probe_task", reinterpret_cast<void*>(&task_update)},
        {kStarted, "cine_probe_started", reinterpret_cast<void*>(&started_read)},
        {kComponent, "cine_probe_component", reinterpret_cast<void*>(&component_check)},
        {kFinal, "cine_probe_final", reinterpret_cast<void*>(&final_check)},
        {kParticipant, "cine_probe_participant", reinterpret_cast<void*>(&participant_check)},
        {kTagTest, "cine_probe_tag_test", reinterpret_cast<void*>(&tag_check)},
        {kSeqRequest, "cine_probe_seq_request", reinterpret_cast<void*>(&seq_request)},
        {kTick, "cine_probe_tick", reinterpret_cast<void*>(&tick_advance)},
        {kPick, "cine_probe_pick", reinterpret_cast<void*>(&pick_variant)},
        {kAlive, "cine_probe_alive", reinterpret_cast<void*>(&alive_check)},
        {kClock, "cine_probe_clock", reinterpret_cast<void*>(&clock_read)},
        {kDuration, "cine_probe_duration", reinterpret_cast<void*>(&duration_read)},
        {kHeal, "cine_probe_heal", reinterpret_cast<void*>(&mode_heal)},
        {kCreate, "cine_probe_create", reinterpret_cast<void*>(&entry_create)},
        {kBind, "cine_probe_bind", reinterpret_cast<void*>(&rig_bind)},
        {kReset, "cine_probe_reset", reinterpret_cast<void*>(&reset_request)},
    }};
    std::byte* const getter = scan_main_image_unique(kStateGetter, "cine_probe_state_getter");
    if (getter == nullptr) {
        return fail("cine_probe_state_getter");
    }
    g_stateGetter = reinterpret_cast<StateTagGetter>(getter);
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
    core::log::write(
        core::log::Channel::client, core::log::Level::info, "ev=probe stage=cine result=installed");
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

} // namespace sunrise::client::hooks::cine_probe
