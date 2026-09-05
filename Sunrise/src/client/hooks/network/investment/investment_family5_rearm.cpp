/**
 * Arms one derived-state rebuild after the family-five object commit. The account's unlock
 * overrides reach that object only when the commit finishes, so a rebuild armed any earlier
 * reads an override list that is not there yet.
 */

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

#include "../../../../core/logging/log.h"
#include "../../../../state/investment/investment.h"
#include "../../../hooking/detour.h"
#include "../../../targets/game/content.h"
#include "internal.h"

namespace sunrise::client::hooks::network::investment {
namespace {

/**
 * The family-five object commit. Its prologue alone matches a dozen sites, some of which differ
 * only in their call displacements, so the exact trailing run is what makes this unique. Every
 * relative displacement is a wildcard.
 */
constexpr std::string_view kCommitSignatureText =
    "48 89 5C 24 ? 57 48 83 EC ? 48 8B DA 48 8B F9 E8 ? ? ? ? 48 8B C8 48 8B D3 E8 ? ? ? ? 48 8D "
    "44 24 ? C6 47 50 02 A8 03 75 ?";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kCommitSignature =
    signature<signature_length(kCommitSignatureText)>(kCommitSignatureText);

/** Result returned when the trampoline is gone, so no commit ran. */
constexpr std::int64_t kNoCommit = 0;
using CommitFamily5 = std::int64_t(__fastcall*)(void*, std::uint64_t*);

hooking::detour::Handle g_handle{};
std::atomic<CommitFamily5> g_original{nullptr};
std::atomic<void*> g_manager{nullptr};
std::atomic_bool g_reportedArm{false};

constexpr std::size_t kObjectArraysOffset = 33'624;
constexpr std::size_t kObjectArraysSize = 878'184;
constexpr std::size_t kDescriptorSize = 16;
constexpr std::size_t kFamily5Type = 5;
constexpr std::size_t kFamily5Slot = 5;
constexpr std::uint32_t kFamily5Stride = 1'712;
constexpr std::uint64_t kFamily5Soid =
    static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
constexpr std::size_t kFlagListOffset = 124;
constexpr std::size_t kValueListOffset = 528;

struct SlotDescriptor {
    std::uint32_t base{};
    std::uint32_t count{};
    std::uint32_t stride{};
    std::uint32_t schemaId{};
};

struct FlagRow {
    std::int16_t slot{};
    std::int8_t value{};
    std::uint8_t padding{};
};

struct ValueRow {
    std::int16_t slot{};
    std::array<std::uint8_t, 2> padding{};
    std::int32_t value{};
};

static_assert(sizeof(SlotDescriptor) == 16);
static_assert(sizeof(FlagRow) == 4);
static_assert(sizeof(ValueRow) == 8);

using ObjectStoreGetter = std::byte*(__fastcall*)();

/**
 * Runs the family-five commit, then arms one derived-state rebuild. The two callers pass different
 * second arguments, so it is passed on unread. Arming twice is harmless, and the next freshness
 * verdict uses it up, so repeat commits need no latch.
 * @param manager Borrowed queuez manager owning the Family-5 commit.
 * @param nested4 Borrowed caller-owned argument, passed on unread.
 * @return The commit's own result, or the no-commit result when the trampoline is gone.
 */
__declspec(noinline) std::int64_t __fastcall commit(void* manager,
                                                    std::uint64_t* nested4) noexcept {
    const CommitFamily5 original = g_original.load(std::memory_order_acquire);
    if (original == nullptr) {
        return kNoCommit;
    }
    g_manager.store(manager, std::memory_order_release);
    // Arm on the way out: the overrides are in the object only once the commit has run.
    const std::int64_t result = original(manager, nested4);
    arm_derived_rebuild();
    if (!g_reportedArm.exchange(true, std::memory_order_relaxed)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=investment stage=family5_commit result=armed");
    }
    return result;
}

} // namespace

bool publish_live_family5(const state::Family5State& family) noexcept {
    const auto& targets = client::targets::game::content::get();
    if (!client::targets::game::content::is_resolved()
        || targets.queuezObjectStoreGetter == nullptr
        || family.objectSoid != kFamily5Soid || family.flagCount > family.flags.size()
        || family.valueCount > family.values.size()) {
        return false;
    }
    const auto getter = reinterpret_cast<ObjectStoreGetter>(targets.queuezObjectStoreGetter);
    std::byte* const store = getter();
    if (store == nullptr) {
        return false;
    }
    const std::size_t descriptorIndex =
        kFamily5Slot + 6U * (kFamily5Type + targets.queuezDescriptorFamilyBias);
    SlotDescriptor descriptor{};
    std::memcpy(&descriptor,
                store + descriptorIndex * kDescriptorSize,
                sizeof descriptor);
    if (descriptor.base > kObjectArraysSize - kFamily5Stride || descriptor.count != 1
        || descriptor.stride != kFamily5Stride) {
        return false;
    }
    std::byte* const object = store + kObjectArraysOffset + descriptor.base;
    std::uint64_t objectSoid = 0;
    std::memcpy(&objectSoid, object, sizeof objectSoid);
    if (objectSoid != kFamily5Soid) {
        return false;
    }

    const CommitFamily5 original = g_original.load(std::memory_order_acquire);
    void* const manager = g_manager.load(std::memory_order_acquire);
    if (original == nullptr || manager == nullptr) {
        return false;
    }

    alignas(16) std::array<std::byte, kFamily5Stride> updated{};
    std::memcpy(updated.data(), object, updated.size());
    std::array<FlagRow, state::kUnlockOverrideCapacity> flags{};
    for (std::size_t index = 0; index < family.flagCount; ++index) {
        flags[index].slot = static_cast<std::int16_t>(family.flags[index].slot);
        flags[index].value = static_cast<std::int8_t>(family.flags[index].value);
    }
    std::array<ValueRow, state::kUnlockOverrideCapacity> values{};
    for (std::size_t index = 0; index < family.valueCount; ++index) {
        values[index].slot = static_cast<std::int16_t>(family.values[index].slot);
        values[index].value = family.values[index].value;
    }
    const auto flagCount = static_cast<std::uint32_t>(family.flagCount);
    const auto valueCount = static_cast<std::uint32_t>(family.valueCount);
    std::memcpy(
        updated.data() + kFlagListOffset + sizeof flagCount, flags.data(), sizeof flags);
    std::memcpy(
        updated.data() + kValueListOffset + sizeof valueCount, values.data(), sizeof values);
    std::memcpy(updated.data() + kFlagListOffset, &flagCount, sizeof flagCount);
    std::memcpy(updated.data() + kValueListOffset, &valueCount, sizeof valueCount);
    (void)commit(manager, reinterpret_cast<std::uint64_t*>(updated.data()));
    return true;
}

/**
 * Attaches the family-five commit rearm.
 * @return True when the target is found and the detour attaches.
 */
bool install_family5_rearm() noexcept {
    if (g_handle.attached) {
        return true;
    }
    std::byte* const target = scan_main_image_unique(kCommitSignature, "queuez_family5_commit");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=family5_commit result=fail reason=target");
        return false;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&commit)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=family5_commit result=fail reason=attach");
        return false;
    }
    g_original.store(reinterpret_cast<CommitFamily5>(g_handle.original), std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=investment stage=family5_commit result=ok");
    return true;
}

/** @return True when the family-five commit rearm is absent. */
bool uninstall_family5_rearm() noexcept {
    if (g_handle.attached && !hooking::detour::uninstall(g_handle)) {
        return false;
    }
    g_original.store(nullptr, std::memory_order_release);
    g_manager.store(nullptr, std::memory_order_release);
    g_reportedArm.store(false, std::memory_order_release);
    return true;
}

/** @return True while the family-five commit rearm is attached. */
bool family5_rearm_is_installed() noexcept {
    return g_handle.attached;
}

} // namespace sunrise::client::hooks::network::investment
