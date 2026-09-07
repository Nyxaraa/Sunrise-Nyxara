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

using Instantiate = std::uint32_t*(__fastcall*)(std::uint32_t*,
                                                const void*,
                                                std::int32_t,
                                                std::int32_t) noexcept;
using Destroy = std::uintptr_t(__fastcall*)(std::uint32_t) noexcept;
using ResolvePair = std::uintptr_t(__fastcall*)(HandlePair*, std::uint32_t) noexcept;
using ValidatePair = std::int32_t*(__fastcall*)(const HandlePair*, std::int32_t*) noexcept;

/** Guards every table, identity count, report budget and detour handle below. */
extern SRWLOCK g_lock;
extern std::atomic_uint32_t g_activeCalls;
extern std::atomic_bool g_accepting;
extern std::size_t g_liveCount;
extern ResolvePair g_resolvePair;
extern ValidatePair g_validatePair;
extern const std::uintptr_t* g_datumBaseStorage;
extern const std::uint32_t* g_datumStrideStorage;

extern std::atomic<Instantiate> g_instantiateOriginal;
extern std::atomic<Destroy> g_destroyOriginal;
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

/** Calls native construction first, then retains the successfully initialized identity. */
__declspec(noinline) std::uint32_t* __fastcall instantiate(std::uint32_t* output,
                                                           const void* entry,
                                                           std::int32_t objectListTag,
                                                           std::int32_t entryIndex) noexcept;

/** Removes the identity while its datum and generation still belong to this handle. */
__declspec(noinline) std::uintptr_t __fastcall destroy(std::uint32_t handle) noexcept;

} // namespace sunrise::client::hooks::world_objects
