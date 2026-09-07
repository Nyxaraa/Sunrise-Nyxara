#include "internal.h"

namespace sunrise::client::hooks::world_objects {
namespace {
void observe_instance(std::uint32_t handle,
                      std::int32_t objectListTag,
                      std::int32_t entryIndex) noexcept {
    if (handle == kNone || objectListTag == -1 || entryIndex == -1 || g_resolvePair == nullptr
        || !g_accepting.load(std::memory_order_acquire)) {
        return;
    }
    HandlePair pair{};
    __try {
        g_resolvePair(&pair, handle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (pair.handle != handle || pair.generation == kNone) {
        return;
    }
    // Native init has already copied placed entry +0x70 into the datum at +0x90, so the identity
    // that survives a mirrored object list is readable here.
    DatumIdentity datum{};
    const bool datumRead = read_datum_identity(handle, datum) && datum.selfHandle == handle
                           && datum.objectListTag == static_cast<std::uint32_t>(objectListTag)
                           && datum.entryIndex == static_cast<std::uint32_t>(entryIndex);
    const Instance instance{datumRead ? datum.placementIdentity : 0,
                            static_cast<std::uint32_t>(objectListTag),
                            static_cast<std::uint32_t>(entryIndex),
                            handle,
                            pair.generation};
    AcquireSRWLockExclusive(&g_lock);
    retain(instance);
    ReleaseSRWLockExclusive(&g_lock);
}
} // namespace
std::atomic<Instantiate> g_instantiateOriginal{nullptr};
std::atomic<Destroy> g_destroyOriginal{nullptr};

__declspec(noinline) std::uint32_t* __fastcall instantiate(std::uint32_t* output,
                                                           const void* entry,
                                                           std::int32_t objectListTag,
                                                           std::int32_t entryIndex) noexcept {
    ActiveCall active;
    const Instantiate original = g_instantiateOriginal.load(std::memory_order_acquire);
    std::uint32_t* const result =
        original != nullptr ? original(output, entry, objectListTag, entryIndex) : output;
    if (result != nullptr) observe_instance(*result, objectListTag, entryIndex);
    return result;
}

__declspec(noinline) std::uintptr_t __fastcall destroy(std::uint32_t handle) noexcept {
    ActiveCall active;
    if (g_accepting.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&g_lock);
        erase(handle);
        ReleaseSRWLockExclusive(&g_lock);
    }
    const Destroy original = g_destroyOriginal.load(std::memory_order_acquire);
    return original != nullptr ? original(handle) : 0;
}

} // namespace sunrise::client::hooks::world_objects
