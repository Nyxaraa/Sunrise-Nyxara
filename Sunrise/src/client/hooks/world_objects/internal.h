#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../memory/current_process_memory.h"
#include "../../patterns/registry.h"
#include "world_object_registry.h"

namespace sunrise::client::hooks::world_objects {

/** Native invalid value for a handle, a generation and a record index. */
inline constexpr std::uint32_t kNone = 0xFFFFFFFFU;
/** Entity indices occupy the low thirteen bits of a glue or network token. */
inline constexpr std::uint32_t kEntityIndexMask = 0x1FFFU;
/** Dynamic handles tracked for teardown. One Tower bubble builds at most 73 of them. */
inline constexpr std::size_t kDynamicHandleCapacity = 256;
/** Per-record diagnostics are bounded; mask-word logging retains the complete selection. */
inline constexpr std::size_t kPurgeTraceCapacity = 64;

/** Datum field offsets the game's own layout fixes; the trace reads them by address. */
inline constexpr std::size_t kDatumFlagsOffset = 0x04;
inline constexpr std::size_t kDatumSelfHandleOffset = 0x0C;
inline constexpr std::size_t kDatumObjectListTagOffset = 0x88;
inline constexpr std::size_t kDatumEntryIndexOffset = 0x8C;
inline constexpr std::size_t kDatumPlacementIdentityOffset = 0x90;

struct DatumIdentity final {
    std::array<std::byte, 12> prefix{};
    std::uint32_t selfHandle{};
    std::array<std::byte, 120> middle{};
    std::uint32_t objectListTag{};
    std::uint32_t entryIndex{};
    std::uint64_t placementIdentity{};
};

static_assert(offsetof(DatumIdentity, selfHandle) == kDatumSelfHandleOffset);
static_assert(offsetof(DatumIdentity, objectListTag) == kDatumObjectListTagOffset);
static_assert(offsetof(DatumIdentity, entryIndex) == kDatumEntryIndexOffset);
static_assert(offsetof(DatumIdentity, placementIdentity) == kDatumPlacementIdentityOffset);

struct HandlePair final {
    std::uint32_t generation{kNone};
    std::uint32_t handle{kNone};
};

/** Last native policy value seen for one bounded glue slot. */
struct PolicyTrace final {
    std::uint32_t glue{}, policy{};
    bool reported{};
};

using Instantiate = std::uint32_t*(__fastcall*)(std::uint32_t*,
                                                const void*,
                                                std::int32_t,
                                                std::int32_t) noexcept;
using Destroy = std::uintptr_t(__fastcall*)(std::uint32_t) noexcept;
using Allocate = Instantiate;
using LogicalDestroy = std::uintptr_t(__fastcall*)(std::uint32_t) noexcept;
using CreateEntity = bool(__fastcall*)(void*, const void*, std::uint32_t, std::uint32_t);
using EntityPolicy = std::uint32_t(__fastcall*)(void*, std::uint32_t);
using PurgeEntities = void(__fastcall*)(void*,
                                        std::int32_t,
                                        const std::uint32_t*,
                                        std::uint32_t*,
                                        std::uint32_t*,
                                        std::uint32_t*,
                                        std::uint8_t);
using ResolvePair = std::uintptr_t(__fastcall*)(HandlePair*, std::uint32_t) noexcept;
using ValidatePair = std::int32_t*(__fastcall*)(const HandlePair*, std::int32_t*) noexcept;

/** Native diagnostic entry points retain their original Windows x64 return registers. */
using Observer = std::uintptr_t(__fastcall*)(void*, void*, const std::uint8_t*);
using Rebind = std::uintptr_t(__fastcall*)();
using IteratorValue = std::uintptr_t(__fastcall*)(void*, std::uint32_t*);
using SourceRef = std::uint8_t(__fastcall*)(std::uint32_t, void*);
using ResolveSource = std::uint8_t(__fastcall*)(const void*, void*);
using Predicate = std::uintptr_t(__fastcall*)(std::uint32_t);
using BindActor = void(__fastcall*)(void*, std::uint32_t);
using Teardown = std::uintptr_t(__fastcall*)(void*);
using ActorOwner = std::uint32_t*(__fastcall*)(std::uint32_t*, std::uint32_t);
using SliceManager = void*(__fastcall*)();
using CurrentBubble = std::uint32_t*(__fastcall*)(void*, std::uint32_t*);

/** Guards every table, identity count, report budget and detour handle below. */
extern SRWLOCK g_lock;
extern std::atomic_uint32_t g_activeCalls;
extern std::atomic_bool g_accepting;
/** Main module base, so a caller address is reported as an RVA that matches the image. */
extern std::uintptr_t g_moduleBase;
extern std::size_t g_liveCount;
extern std::size_t g_identityReportBudget;
extern std::size_t g_dynamicReportBudget;
extern std::size_t g_allocateReportBudget;
extern std::size_t g_logicalDestroyReportBudget;
extern std::uint64_t g_dynamicCount;
extern std::array<std::uint32_t, kDynamicHandleCapacity> g_dynamicHandles;
extern std::array<std::uint64_t, kDynamicHandleCapacity> g_dynamicOrdinals;
extern ResolvePair g_resolvePair;
extern ValidatePair g_validatePair;
extern const std::uintptr_t* g_datumBaseStorage;
extern const std::uint32_t* g_datumStrideStorage;

extern std::atomic<Instantiate> g_instantiateOriginal;
extern std::atomic<Destroy> g_destroyOriginal;
extern std::atomic<Allocate> g_allocateOriginal;
extern std::atomic<LogicalDestroy> g_logicalDestroyOriginal;

extern std::atomic<CreateEntity> g_createEntityOriginal;
extern std::atomic<PurgeEntities> g_purgeEntitiesOriginal;
extern std::atomic<EntityPolicy> g_entityPolicyOriginal;
extern std::uintptr_t g_entityRecordBase;
extern std::array<PolicyTrace, kPurgeTraceCapacity> g_policyTrace;
extern const std::uintptr_t* g_glueBaseStorage;
extern const std::uint32_t* g_glueStrideStorage;
/** Native entity identity of the create call this thread is inside. */
extern thread_local std::uint32_t t_entityGlue;
extern thread_local std::uint32_t t_entityNetwork;

extern std::atomic<Observer> g_observerOriginal;
extern std::atomic<Rebind> g_rebindOriginal;
extern std::atomic<IteratorValue> g_iteratorOriginal;
extern std::atomic<SourceRef> g_sourceOriginal;
extern std::atomic<ResolveSource> g_resolveSourceOriginal;
extern std::atomic<Predicate> g_predicateOriginal;
extern std::atomic<BindActor> g_bindOriginal;
extern std::atomic<Teardown> g_teardownOriginal;
extern ActorOwner g_actorOwner;
extern const std::uintptr_t* g_actorBaseStorage;
extern const std::uint32_t* g_actorStrideStorage;
extern SliceManager g_sliceManager;
extern CurrentBubble g_currentBubble;
/** Rebind callers are checked against their resolved function-relative return offsets. */
extern std::uintptr_t g_rebindAddress, g_observerAddress;
extern std::atomic_uint32_t g_rebindPasses, g_rebindRows, g_observerReports;

/** Reads one scalar at an address in this process, with no pointer round trip. */
template <typename Value>
[[nodiscard]] bool read_at(std::uintptr_t address, Value& output) noexcept {
    return memory::read_current_process(
        nullptr, address, std::span(reinterpret_cast<std::byte*>(&output), sizeof output));
}

/** Reads one scalar from the process without trusting a stale game-owned pointer. */
template <typename Value>
[[nodiscard]] bool read_value(const Value* source, Value& output) noexcept {
    return read_at(reinterpret_cast<std::uintptr_t>(source), output);
}

/** Counts one in-flight detour call, so teardown can wait for the hooks to drain. */
class ActiveCall final {
public:
    ActiveCall() noexcept {
        g_activeCalls.fetch_add(1, std::memory_order_acq_rel);
    }
    ~ActiveCall() {
        g_activeCalls.fetch_sub(1, std::memory_order_acq_rel);
    }

    ActiveCall(const ActiveCall&) = delete;
    ActiveCall& operator=(const ActiveCall&) = delete;
};

/** @return True when the datum retained a real placed-entry identity. */
[[nodiscard]] constexpr bool has_placement_identity(const Instance& instance) noexcept {
    return instance.placementIdentity != 0
           && instance.placementIdentity != kAbsentPlacementIdentity;
}

/** Inserts or replaces one handle without allocating. Caller owns g_lock exclusively. */
void retain(const Instance& instance) noexcept;

/** Erases one handle before native teardown recycles its generation. Caller owns g_lock. */
void erase(std::uint32_t handle) noexcept;

/**
 * Reads one object datum's identity block. Takes no lock.
 * @param handle Native object handle.
 * @param output Receives the identity block; the placement identity stays zero on a short stride.
 * @return False when the datum globals or the datum itself cannot be read.
 */
[[nodiscard]] bool read_datum_identity(std::uint32_t handle, DatumIdentity& output) noexcept;

/** @return Signatures the entity create, purge and policy hooks need, in resolve order. */
[[nodiscard]] std::span<const patterns::Pattern> entity_patterns() noexcept;

/** @return Signatures the squad rebind and observer trace needs, in resolve order. */
[[nodiscard]] std::span<const patterns::Pattern> trace_patterns() noexcept;

/**
 * Decodes the actor iterator from the rebind body's own call, and checks the other four calls
 * still reach the targets the image pass resolved.
 * @param rebind Resolved rebind body.
 * @param source Resolved source lookup.
 * @param resolveSource Resolved source resolver.
 * @param predicate Resolved predicate dispatch.
 * @param bind Resolved actor bind.
 * @param iterator Receives the iterator; null when any call site moved.
 * @return True when every call site holds a near call to its expected target.
 */
[[nodiscard]] bool bind_rebind_calls(std::byte* rebind,
                                     std::byte* source,
                                     std::byte* resolveSource,
                                     std::byte* predicate,
                                     std::byte* bind,
                                     std::byte*& iterator) noexcept;

/** Calls native construction first, then retains the successfully initialized identity. */
__declspec(noinline) std::uint32_t* __fastcall instantiate(std::uint32_t* output,
                                                           const void* entry,
                                                           std::int32_t objectListTag,
                                                           std::int32_t entryIndex) noexcept;

/** Calls native allocation first, then reports it when the instantiate hook did not ask. */
__declspec(noinline) std::uint32_t* __fastcall allocate(std::uint32_t* output,
                                                        const void* entry,
                                                        std::int32_t objectListTag,
                                                        std::int32_t entryIndex) noexcept;

/** Reports one logical destroy, the only teardown that releases the object's entity. */
__declspec(noinline) std::uintptr_t __fastcall logical_destroy(std::uint32_t handle) noexcept;

/** Removes the identity while its datum and generation still belong to this handle. */
__declspec(noinline) std::uintptr_t __fastcall destroy(std::uint32_t handle) noexcept;

/** Carries native entity identity into the allocator trace without changing creation. */
__declspec(noinline) bool __fastcall create_entity(void* definition,
                                                   const void* data,
                                                   std::uint32_t glue,
                                                   std::uint32_t parent);

/** Reports native policy changes for the bounded glue slots under investigation. */
__declspec(noinline) std::uint32_t __fastcall entity_policy(void* definition, std::uint32_t glue);

/** Logs the mask the native purge actually consumes and preserves all seven arguments. */
__declspec(noinline) void __fastcall purge_entities(void* view,
                                                    std::int32_t reason,
                                                    const std::uint32_t* mask,
                                                    std::uint32_t* work0,
                                                    std::uint32_t* work1,
                                                    std::uint32_t* work2,
                                                    std::uint8_t epoch);

/** Preserves the observer's original call and records the bubble used by its native gate. */
__declspec(noinline) std::uintptr_t __fastcall trace_observer(void* observer,
                                                              void* activity,
                                                              const std::uint8_t* bubble);

/** Nested calls restore their caller's diagnostic scope without changing native execution. */
__declspec(noinline) std::uintptr_t __fastcall trace_rebind();

/** Captures only the rebind function's direct actor iterator call. */
__declspec(noinline) std::uintptr_t __fastcall trace_iterator(void* iterator,
                                                              std::uint32_t* output);

/** Associates the actual source lookup with the current iterator output. */
__declspec(noinline) std::uint8_t __fastcall trace_source(std::uint32_t owner, void* output);

/** Records only the source resolver called directly by the current rebind pass. */
__declspec(noinline) std::uint8_t __fastcall trace_resolve(const void* source, void* output);

/** Records the native exclusion predicate without invoking it a second time. */
__declspec(noinline) std::uintptr_t __fastcall trace_predicate(std::uint32_t owner);

/** Records collection counts and the actor's binding around the actual native insertion call. */
__declspec(noinline) void __fastcall trace_bind(void* squad, std::uint32_t actor);

/** Captures generation-valid member owners before native teardown and checks them afterward. */
__declspec(noinline) std::uintptr_t __fastcall trace_teardown(void* squad);

} // namespace sunrise::client::hooks::world_objects
