// Squad rebind and observer diagnostics for the world-object hooks.
// These detours take no lock. They read game memory and thread-local trace state only.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <intrin.h>
#include <span>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/registry.h"
#include "../../patterns/signature_text.h"
#include "internal.h"
#include "world_object_rebind_trace.h"

namespace sunrise::client::hooks::world_objects {

std::atomic<Observer> g_observerOriginal{};
std::atomic<Rebind> g_rebindOriginal{};
std::atomic<IteratorValue> g_iteratorOriginal{};
std::atomic<SourceRef> g_sourceOriginal{};
std::atomic<ResolveSource> g_resolveSourceOriginal{};
std::atomic<Predicate> g_predicateOriginal{};
std::atomic<BindActor> g_bindOriginal{};
std::atomic<Teardown> g_teardownOriginal{};
ActorOwner g_actorOwner{};
const std::uintptr_t* g_actorBaseStorage{};
const std::uint32_t* g_actorStrideStorage{};
SliceManager g_sliceManager{};
CurrentBubble g_currentBubble{};
std::uintptr_t g_rebindAddress{}, g_observerAddress{};
std::atomic_uint32_t g_rebindPasses{}, g_rebindRows{}, g_observerReports{};

namespace {

using patterns::signature;
using patterns::signature_length;

/** Each diagnostic run admits 128 passes and 4096 actor/member rows. */
constexpr std::uint32_t kRebindPassBudget = 128, kRebindRowBudget = 4096;
/** Native squad collections contain at most 80 generation-checked eight-byte actor references. */
constexpr std::size_t kSquadMembers = 80, kSquadCountOffset = 0x314, kSquadRefsOffset = 0x318;
/** The actor record holds its binding as two qwords; a shorter stride cannot carry them. */
constexpr std::uintptr_t kActorBindingOffset = 0x38;
constexpr std::uint32_t kActorBindingStride = 0x48;
/** Return addresses of the rebind body's five direct calls, as offsets from its start. */
constexpr std::uintptr_t kIteratorReturn = 0x5F;
constexpr std::uintptr_t kSourceReturn = 0x83;
constexpr std::uintptr_t kResolveReturn = 0xA5;
constexpr std::uintptr_t kPredicateReturn = 0x103;
constexpr std::uintptr_t kBindReturn = 0x135;
/** A near call is five bytes: the E8 sits five before its return address. */
constexpr std::uintptr_t kNearCallBytes = 5;
constexpr std::byte kCallOpcode{0xE8};

struct ActorSource final {
    std::uint32_t key{kNone};
    std::uint8_t type{0xFF}, padding{};
    std::uint16_t index{0xFFFF};
};
/** One actor source reference is eight bytes on the wire the game walks. */
constexpr std::size_t kActorSourceBytes = 8;
static_assert(sizeof(ActorSource) == kActorSourceBytes);

struct RebindTrace final {
    trace::Association association{};
    ActorSource source{};
    void* activity{};
    std::uint32_t bubble{kNone}, pass{}, visited{}, bound{};
    int sourceResult{-1}, resolveResult{-1}, predicate{-1};
    std::int32_t countBefore{-1}, countAfter{-1};
    std::array<std::uint64_t, 2> bindingBefore{}, bindingAfter{};
    bool bindCalled{}, bindingBeforeKnown{}, bindingAfterKnown{};
    std::uint32_t flags{}, flagsAfter{};
    bool flagsKnown{}, flagsAfterKnown{}, sourceKnown{};
};
thread_local RebindTrace* t_rebindTrace{};

struct ObserverTrace final {
    void* activity{};
    std::uint32_t bubble{kNone};
};
thread_local ObserverTrace* t_observerTrace{};

/** Private signatures select the verified native diagnostic ABI. */
constexpr std::string_view kObserverTraceText =
    "48 89 5C 24 08 57 48 83 EC 20 49 8B F8 48 8B DA E8 ? ? ? ? 48 8B C8 48 8D 54 24 38 E8 ? ? ? ? "
    "E8 ? ? ? ? 48 3B D8 75 13";
/** Compiled form of that pattern; its length fixes the two operands read at +17 and +30. */
constexpr auto kObserverTracePattern =
    signature<signature_length(kObserverTraceText)>(kObserverTraceText);
/**
 * Native entry the rebind pass runs, detoured for the trace. A sibling body shares its first 61
 * bytes, so the pattern runs past the first branch. The iterator it calls has no unique shape
 * of its own and is decoded from the call at `kIteratorReturn`.
 */
constexpr std::string_view kRebindTraceText =
    "48 89 5C 24 10 48 89 7C 24 18 55 48 8D AC 24 30 FF FF FF 48 81 EC D0 01 00 00 48 8B 05 ? ? ? "
    "? 48 33 C4 48 89 85 C0 00 00 00 33 FF 48 8D 4C 24 40 33 D2 89 7C 24 70 E8 ? ? ? ? 48 8D 4C "
    "24 40 E8 ? ? ? ? 84 C0 0F 84 1E 01 00 00 90 48 8D 54 24 24 48 8D 4C 24 40 E8 ? ? ? ?";
/** Compiled form of that pattern; the scan requires one match. */
constexpr auto kRebindTracePattern =
    signature<signature_length(kRebindTraceText)>(kRebindTraceText);
/** Native source lookup the rebind pass calls, detoured for the trace. */
constexpr std::string_view kSourceTraceText =
    "48 89 5C 24 10 56 48 83 EC 20 48 8B F2 8B D9 83 F9 FF 0F 84 8C 00 00 00 48 89 7C 24 30";
/** Compiled form of that pattern; the scan requires one match. */
constexpr auto kSourceTracePattern =
    signature<signature_length(kSourceTraceText)>(kSourceTraceText);
/** Native entry that resolves a source to its record, detoured for the trace. */
constexpr std::string_view kResolveSourceTraceText =
    "40 53 48 83 EC 20 48 0F BE 41 04 48 8B DA 83 F8 3C 77 17 48 BA 00 00 00 00 00 B0 01 18";
/** Compiled form of that pattern; the scan requires one match. */
constexpr auto kResolveSourceTracePattern =
    signature<signature_length(kResolveSourceTraceText)>(kResolveSourceTraceText);
/** Native predicate dispatch the rebind pass calls, detoured for the trace. */
constexpr std::string_view kPredicateTraceText =
    "48 8B 05 ? ? ? ? 8B D1 48 8B C8 4C 8B 00 49 FF A0 08 01 00 00";
/** Compiled form of that pattern; the scan requires one match. */
constexpr auto kPredicateTracePattern =
    signature<signature_length(kPredicateTraceText)>(kPredicateTraceText);
/** Native entry that binds one actor into a squad, detoured for the trace. */
constexpr std::string_view kBindTraceText = "48 89 5C 24 08 57 48 83 EC 30 48 8B F9 8B DA 48 81 C1 "
                                            "F0 02 00 00 E8 ? ? ? ? 84 C0 0F 84 97 00 00 00";
/** Compiled form of that pattern; the scan requires one match. */
constexpr auto kBindTracePattern = signature<signature_length(kBindTraceText)>(kBindTraceText);
/** Native entry that tears a squad down, detoured for the trace. */
constexpr std::string_view kTeardownTraceText =
    "40 56 48 83 EC 40 83 B9 FC 05 00 00 FF 48 8B F1 0F 84 27 01 00 00";
/** Compiled form of that pattern; the scan requires one match. */
constexpr auto kTeardownTracePattern =
    signature<signature_length(kTeardownTraceText)>(kTeardownTraceText);
/** Native entry that returns an actor's owner. */
constexpr std::string_view kActorOwnerTraceText =
    "C7 01 FF FF FF FF 83 FA FF 74 1B 81 E2 FF 1F 00 00 0F AF 15 ? ? ? ? 8B C2 48 03 05 ? ? ? ? 8B "
    "50 4C 89 11 48 8B C1 C3";
/** Compiled form of that pattern; its length fixes the two operands read at +20 and +29. */
constexpr auto kActorOwnerTracePattern =
    signature<signature_length(kActorOwnerTraceText)>(kActorOwnerTraceText);

/** The eight trace signatures, in the order the registry's target list expects them. */
constexpr std::array kTraceSignatures{
    patterns::Pattern{"squad_trace_observer", kObserverTracePattern},
    patterns::Pattern{"squad_trace_rebind", kRebindTracePattern},
    patterns::Pattern{"squad_trace_source", kSourceTracePattern},
    patterns::Pattern{"squad_trace_resolveSource", kResolveSourceTracePattern},
    patterns::Pattern{"squad_trace_predicate", kPredicateTracePattern},
    patterns::Pattern{"squad_trace_bind", kBindTracePattern},
    patterns::Pattern{"squad_trace_teardown", kTeardownTracePattern},
    patterns::Pattern{"squad_trace_actorOwner", kActorOwnerTracePattern},
};

/** Reads flags only when the datum still names the observed full handle. */
bool trace_flags(std::uint32_t handle, std::uint32_t& flags) noexcept {
    std::uintptr_t base{};
    std::uint32_t stride{}, self{};
    if (handle == kNone || !read_value(g_datumBaseStorage, base)
        || !read_value(g_datumStrideStorage, stride) || !base || stride < 16) {
        return false;
    }
    const auto datum = base + static_cast<std::uintptr_t>(stride) * (handle & kEntityIndexMask);
    return read_at(datum + kDatumSelfHandleOffset, self) && self == handle
           && read_at(datum + kDatumFlagsOffset, flags);
}

/** Logs a bounded line without trusting the formatter's required buffer length. */
template <typename... Args> void trace_log(const char* format, Args... args) noexcept {
    std::array<char, 640> line{};
    const int length = std::snprintf(line.data(), line.size(), format, args...);
    if (length > 0) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::debug,
            {line.data(), std::min(static_cast<std::size_t>(length), line.size() - 1)});
    }
}

/** Flushes the results of actual native calls for one actor visited by the rebind pass. */
void flush_rebind_actor(RebindTrace& value) noexcept {
    if (value.association.actor == kNone) {
        return;
    }
    if (g_rebindRows.fetch_add(1) < kRebindRowBudget) {
        value.flagsAfterKnown = trace_flags(value.association.owner, value.flagsAfter);
        trace_log("ev=world_object stage=squad_rebind_actor pass=%u activity=%p bubble=%u "
                  "actor=0x%08X owner=0x%08X flags=0x%08X flags_known=%u after=0x%08X "
                  "after_known=%u source_result=%d source_known=%u key=0x%08X type=%u index=%u "
                  "resolved=%d predicate=%d bind_called=%u count_before=%d count_after=%d "
                  "binding_before_known=%u binding_after_known=%u before0=0x%016llX "
                  "before1=0x%016llX after0=0x%016llX after1=0x%016llX",
                  value.pass,
                  value.activity,
                  value.bubble,
                  value.association.actor,
                  value.association.owner,
                  value.flags,
                  value.flagsKnown,
                  value.flagsAfter,
                  value.flagsAfterKnown,
                  value.sourceResult,
                  value.sourceKnown,
                  value.source.key,
                  value.source.type,
                  value.source.index,
                  value.resolveResult,
                  value.predicate,
                  value.bindCalled,
                  value.countBefore,
                  value.countAfter,
                  value.bindingBeforeKnown,
                  value.bindingAfterKnown,
                  value.bindingBefore[0],
                  value.bindingBefore[1],
                  value.bindingAfter[0],
                  value.bindingAfter[1]);
    }
    value.association.actor = kNone;
}

/** Reads a binding only for the actual generation-valid actor selected by native iteration. */
bool trace_actor_binding(std::uint32_t actor, std::array<std::uint64_t, 2>& output) noexcept {
    std::uintptr_t base{};
    std::uint32_t stride{};
    if (actor == kNone || !read_value(g_actorBaseStorage, base)
        || !read_value(g_actorStrideStorage, stride) || !base || stride < kActorBindingStride) {
        return false;
    }
    return read_at(base + static_cast<std::uintptr_t>(stride) * (actor & kEntityIndexMask)
                       + kActorBindingOffset,
                   output);
}

/** @return The target of the near call whose return address is `returnOffset`, or null. */
[[nodiscard]] std::byte* rebind_call_target(std::byte* rebind,
                                            std::uintptr_t returnOffset) noexcept {
    std::byte* const site = rebind + returnOffset - kNearCallBytes;
    std::byte opcode{};
    if (!read_at(reinterpret_cast<std::uintptr_t>(site), opcode) || opcode != kCallOpcode) {
        return nullptr;
    }
    return patterns::resolve_relative(site + 1, site + kNearCallBytes);
}

} // namespace

/** @return Signatures the squad rebind and observer trace needs, in resolve order. */
std::span<const patterns::Pattern> trace_patterns() noexcept {
    return kTraceSignatures;
}

/** Decodes the iterator from the rebind body, and refuses a body whose other calls moved. */
bool bind_rebind_calls(std::byte* rebind,
                       std::byte* source,
                       std::byte* resolveSource,
                       std::byte* predicate,
                       std::byte* bind,
                       std::byte*& iterator) noexcept {
    iterator = nullptr;
    if (rebind == nullptr || rebind_call_target(rebind, kSourceReturn) != source
        || rebind_call_target(rebind, kResolveReturn) != resolveSource
        || rebind_call_target(rebind, kPredicateReturn) != predicate
        || rebind_call_target(rebind, kBindReturn) != bind) {
        return false;
    }
    iterator = rebind_call_target(rebind, kIteratorReturn);
    return iterator != nullptr;
}

/** Preserves the observer's original call and records the bubble used by its native gate. */
__declspec(noinline) std::uintptr_t __fastcall trace_observer(void* observer,
                                                              void* activity,
                                                              const std::uint8_t* bubble) {
    ActiveCall active;
    const auto original = g_observerOriginal.load(std::memory_order_acquire);
    ObserverTrace observerTrace{};
    trace::Scope<ObserverTrace> scope(t_observerTrace, observerTrace);
    std::uint8_t requested{0xFF};
    std::uint32_t current{kNone};
    const bool report = g_accepting.load() && g_observerReports.fetch_add(1) < kRebindPassBudget;
    if (report && read_value(bubble, requested)) {
        observerTrace.activity = activity;
        observerTrace.bubble = requested;
        if (g_sliceManager && g_currentBubble) {
            g_currentBubble(g_sliceManager(), &current);
        }
        trace_log("ev=world_object stage=squad_observer activity=%p bubble=%u current=%u",
                  activity,
                  requested,
                  current);
    }
    const auto result = original ? original(observer, activity, bubble) : 0;
    return result;
}

/** Nested calls restore their caller's diagnostic scope without changing native execution. */
__declspec(noinline) std::uintptr_t __fastcall trace_rebind() {
    ActiveCall active;
    const auto original = g_rebindOriginal.load(std::memory_order_acquire);
    RebindTrace value{};
    trace::Scope<RebindTrace> scope(t_rebindTrace, value);
    const auto pass = g_rebindPasses.fetch_add(1);
    value.association.active = g_accepting.load() && pass < kRebindPassBudget;
    value.activity = t_observerTrace ? t_observerTrace->activity : nullptr;
    value.bubble = t_observerTrace ? t_observerTrace->bubble : kNone;
    value.pass = pass;
    const auto result = original ? original() : 0;
    flush_rebind_actor(value);
    if (value.association.active) {
        trace_log("ev=world_object stage=squad_rebind pass=%u activity=%p bubble=%u visited=%u "
                  "bind_calls=%u",
                  pass,
                  value.activity,
                  value.bubble,
                  value.visited,
                  value.bound);
    }
    return result;
}

/** Captures only the rebind function's direct actor iterator call. */
__declspec(noinline) std::uintptr_t __fastcall trace_iterator(void* iterator,
                                                              std::uint32_t* output) {
    ActiveCall active;
    const auto original = g_iteratorOriginal.load(std::memory_order_acquire);
    const auto result = original ? original(iterator, output) : 0;
    auto* value = t_rebindTrace;
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (value && value->association.active && caller == g_rebindAddress + kIteratorReturn
        && (value->association.iterator == 0
            || value->association.iterator == reinterpret_cast<std::uintptr_t>(iterator))) {
        flush_rebind_actor(*value);
        if (value->association.visit(caller,
                                     g_rebindAddress + kIteratorReturn,
                                     reinterpret_cast<std::uintptr_t>(iterator))) {
            value->source = {};
            value->sourceResult = value->resolveResult = value->predicate = -1;
            value->countBefore = value->countAfter = -1;
            value->bindCalled = value->bindingBeforeKnown = value->bindingAfterKnown = false;
            value->bindingBefore = value->bindingAfter = {};
            value->flagsKnown = value->flagsAfterKnown = value->sourceKnown = false;
            value->flags = value->flagsAfter = 0;
            if (read_value(output, value->association.actor) && value->association.actor != kNone) {
                ++value->visited;
                if (g_actorOwner) {
                    g_actorOwner(&value->association.owner, value->association.actor);
                }
                value->flagsKnown = trace_flags(value->association.owner, value->flags);
            }
        }
    }
    return result;
}

/** Associates the actual source lookup with the current iterator output. */
__declspec(noinline) std::uint8_t __fastcall trace_source(std::uint32_t owner, void* output) {
    ActiveCall active;
    const auto original = g_sourceOriginal.load(std::memory_order_acquire);
    const std::uint8_t result = original ? original(owner, output) : std::uint8_t{};
    auto* value = t_rebindTrace;
    if (value && value->association.active && value->association.actor != kNone
        && reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == g_rebindAddress + kSourceReturn) {
        value->association.owner = owner;
        value->sourceResult = result;
        value->flagsKnown = trace_flags(owner, value->flags);
        if (result) {
            value->sourceKnown = read_value(static_cast<const ActorSource*>(output), value->source);
        }
    }
    return result;
}

/** Records only the source resolver called directly by the current rebind pass. */
__declspec(noinline) std::uint8_t __fastcall trace_resolve(const void* source, void* output) {
    ActiveCall active;
    const auto original = g_resolveSourceOriginal.load(std::memory_order_acquire);
    const std::uint8_t result = original ? original(source, output) : std::uint8_t{};
    auto* value = t_rebindTrace;
    ActorSource actual{};
    if (value && value->association.active && value->sourceKnown
        && reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == g_rebindAddress + kResolveReturn
        && read_value(static_cast<const ActorSource*>(source), actual)
        && actual.key == value->source.key && actual.type == value->source.type
        && actual.index == value->source.index) {
        value->resolveResult = result;
    }
    return result;
}

/** Records the native exclusion predicate without invoking it a second time. */
__declspec(noinline) std::uintptr_t __fastcall trace_predicate(std::uint32_t owner) {
    ActiveCall active;
    const auto original = g_predicateOriginal.load(std::memory_order_acquire);
    const auto result = original ? original(owner) : 0;
    auto* value = t_rebindTrace;
    if (value
        && value->association.matches(reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
                                      g_rebindAddress + kPredicateReturn,
                                      owner)) {
        value->predicate = static_cast<std::uint8_t>(result);
    }
    return result;
}

/** Records collection counts and the actor's binding around the actual native insertion call. */
__declspec(noinline) void __fastcall trace_bind(void* squad, std::uint32_t actor) {
    ActiveCall active;
    const auto original = g_bindOriginal.load(std::memory_order_acquire);
    auto* value = t_rebindTrace;
    const bool selected =
        value && value->association.active && value->association.actor == actor
        && reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == g_rebindAddress + kBindReturn;
    const auto count = reinterpret_cast<std::uintptr_t>(squad) + kSquadCountOffset;
    if (selected) {
        value->bindCalled = true;
        ++value->bound;
        static_cast<void>(read_at(count, value->countBefore));
        value->bindingBeforeKnown = trace_actor_binding(actor, value->bindingBefore);
    }
    if (original) {
        original(squad, actor);
    }
    if (selected) {
        static_cast<void>(read_at(count, value->countAfter));
        value->bindingAfterKnown = trace_actor_binding(actor, value->bindingAfter);
    }
}

/** Captures generation-valid member owners before native teardown and checks them afterward. */
__declspec(noinline) std::uintptr_t __fastcall trace_teardown(void* squad) {
    ActiveCall active;
    const auto original = g_teardownOriginal.load(std::memory_order_acquire);
    std::array<std::uint32_t, kSquadMembers> owners{}, flags{};
    std::array<bool, kSquadMembers> known{};
    std::int32_t count{-1};
    const auto base = reinterpret_cast<std::uintptr_t>(squad);
    const bool report = g_accepting.load() && g_rebindRows.load() < kRebindRowBudget
                        && read_at(base + kSquadCountOffset, count) && count >= 0
                        && count <= static_cast<std::int32_t>(kSquadMembers);
    if (report) {
        for (std::int32_t index = 0; index < count; ++index) {
            HandlePair pair{};
            std::int32_t actor{-1};
            owners[index] = kNone;
            if (read_at(base + kSquadRefsOffset + sizeof(HandlePair) * index, pair)
                && g_validatePair && g_actorOwner) {
                g_validatePair(&pair, &actor);
                if (actor != -1) {
                    g_actorOwner(&owners[index], static_cast<std::uint32_t>(actor));
                }
                known[index] = trace_flags(owners[index], flags[index]);
            }
        }
    }
    const auto result = original ? original(squad) : 0;
    if (report && g_rebindRows.fetch_add(1) < kRebindRowBudget) {
        std::int32_t after{-1};
        static_cast<void>(read_at(base + kSquadCountOffset, after));
        trace_log("ev=world_object stage=squad_teardown squad=%p before=%d after=%d",
                  squad,
                  count,
                  after);
        for (std::int32_t index = 0; index < count; ++index) {
            if (g_rebindRows.fetch_add(1) >= kRebindRowBudget) {
                break;
            }
            std::uint32_t afterFlags{};
            const bool afterKnown = trace_flags(owners[index], afterFlags);
            trace_log("ev=world_object stage=squad_teardown_member squad=%p index=%d owner=0x%08X "
                      "before=0x%08X before_known=%u after=0x%08X after_known=%u",
                      squad,
                      index,
                      owners[index],
                      flags[index],
                      known[index],
                      afterFlags,
                      afterKnown);
        }
    }
    return result;
}

} // namespace sunrise::client::hooks::world_objects
