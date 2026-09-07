#include "slice_set_sample.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../../../core/logging/log.h"
#include "../internal.h"

namespace sunrise::client::hooks::bootflow::spawn {
namespace {

/** The player spawn gate. The head alone is not unique, so the pattern runs to the cookie store. */
constexpr std::string_view kSpawnGateSignatureText =
    "40 53 57 41 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 8B D9 "
    "40 B7 01";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kSpawnGateSignature =
    signature<signature_length(kSpawnGateSignatureText)>(kSpawnGateSignatureText);

/** Every site must hold a near call, or the offset is stale. */
constexpr std::byte kCallOpcode{0xE8};
/** Length of a near call, and the offset of its displacement. */
constexpr std::size_t kCallLength = 5;
constexpr std::size_t kCallOperand = 1;
/** Largest index accepted by the client's own slice-set-to-bubble mapper. */
constexpr std::int32_t kMaximumSliceSet = 0x1FF;

using NoArgPointer = void*(__fastcall*)() noexcept;
using PointerPredicate = bool(__fastcall*)(void*) noexcept;
using SliceSetIndexGetter = void*(__fastcall*)(void*, std::int32_t*) noexcept;

std::atomic<NoArgPointer> g_sliceSetManager{nullptr};
std::atomic<PointerPredicate> g_worldPresent{nullptr};
std::atomic<SliceSetIndexGetter> g_currentSliceSet{nullptr};
std::atomic<PointerPredicate> g_sliceSetAddressable{nullptr};

/** Byte offsets of each call from the gate's base. A site without a near call refuses. */
struct Site {
    /** The slice-set manager accessor both world conditions start from. */
    static constexpr std::size_t sliceSetManager = 0x23;
    /** G1: a world is present. */
    static constexpr std::size_t worldPresent = 0x2B;
    /** G2's input: the current slice-set index. */
    static constexpr std::size_t currentSliceSet = 0x49;
    /** G2: that index is addressable. */
    static constexpr std::size_t sliceSetAddressable = 0x51;
};

/**
 * Decodes one call target.
 * @param gate Base of the spawn gate.
 * @param offset Byte offset of the call from the gate base.
 * @return The called address, or null when the site is not a near call.
 */
[[nodiscard]] void* call_target(const std::byte* gate, std::size_t offset) noexcept {
    const std::byte* const site = gate + offset;
    if (*site != kCallOpcode) {
        return nullptr;
    }
    return resolve_relative(site + kCallOperand, site + kCallLength);
}

} // namespace

/** Finds the spawn gate and decodes the four slice-set calls inside it. */
bool install_targets() noexcept {
    uninstall_targets();
    const std::byte* const gate = scan_main_image_unique(kSpawnGateSignature, "player_spawn_gate");
    if (gate == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=current_slice result=fail reason=target");
        return false;
    }
    const auto manager = reinterpret_cast<NoArgPointer>(call_target(gate, Site::sliceSetManager));
    const auto present = reinterpret_cast<PointerPredicate>(call_target(gate, Site::worldPresent));
    const auto current =
        reinterpret_cast<SliceSetIndexGetter>(call_target(gate, Site::currentSliceSet));
    const auto addressable =
        reinterpret_cast<PointerPredicate>(call_target(gate, Site::sliceSetAddressable));
    if (manager == nullptr || present == nullptr || current == nullptr || addressable == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=current_slice result=fail reason=calls");
        return false;
    }
    g_sliceSetManager.store(manager, std::memory_order_release);
    g_worldPresent.store(present, std::memory_order_release);
    g_currentSliceSet.store(current, std::memory_order_release);
    g_sliceSetAddressable.store(addressable, std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=current_slice result=ok");
    return true;
}

/** Clears the calls it found. */
void uninstall_targets() noexcept {
    g_sliceSetAddressable.store(nullptr, std::memory_order_release);
    g_currentSliceSet.store(nullptr, std::memory_order_release);
    g_worldPresent.store(nullptr, std::memory_order_release);
    g_sliceSetManager.store(nullptr, std::memory_order_release);
}

/** Reads the client's current local slice-set index through the spawn gate's own calls. */
std::int32_t sample_current_slice_set() noexcept {
    const NoArgPointer managerGetter = g_sliceSetManager.load(std::memory_order_acquire);
    const PointerPredicate worldPresent = g_worldPresent.load(std::memory_order_acquire);
    const SliceSetIndexGetter current = g_currentSliceSet.load(std::memory_order_acquire);
    const PointerPredicate addressable = g_sliceSetAddressable.load(std::memory_order_acquire);
    if (managerGetter == nullptr || worldPresent == nullptr || current == nullptr
        || addressable == nullptr) {
        return -2;
    }
    void* const manager = managerGetter();
    if (!worldPresent(manager)) {
        return -1;
    }
    std::int32_t index = -1;
    void* const sliceSet = current(manager, &index);
    return addressable(sliceSet) && index >= 0 && index <= kMaximumSliceSet ? index : -1;
}

} // namespace sunrise::client::hooks::bootflow::spawn
