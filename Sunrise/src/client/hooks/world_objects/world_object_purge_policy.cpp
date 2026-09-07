// Simulation entity create, policy and purge hooks for the world-object trace.
// The detours here take g_lock only around g_policyTrace; no caller may hold it on entry.

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../memory/current_process_memory.h"
#include "../../patterns/registry.h"
#include "../../patterns/signature_text.h"
#include "internal.h"

namespace sunrise::client::hooks::world_objects {
namespace {

using patterns::signature;
using patterns::signature_length;

/** Native entry that creates a simulation entity. */
constexpr std::string_view kCreateEntityText =
    "48 89 5C 24 08 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 "
    "8B 05 ? ? ? ?";
/** Native entry that purges simulation entities. */
constexpr std::string_view kPurgeEntitiesText =
    "40 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 "
    "48 89 45 ? 48 8B 75 ? 44 8B FA 4C 8B 75 ? 48 8B F9 BA 00 20 00 00";
/** Native entry that maps a glue token to its entity record. */
constexpr std::string_view kGlueMappingText =
    "81 E1 FF 1F 00 00 0F AF 0D ? ? ? ? 8B C1 48 03 05 ? ? ? ? 89 10 C3";
/** Compiled form of that pattern; the scan requires one match. */
constexpr auto kCreateEntityPattern =
    signature<signature_length(kCreateEntityText)>(kCreateEntityText);
/** Compiled form of that pattern; the scan requires one match. */
constexpr auto kPurgeEntitiesPattern =
    signature<signature_length(kPurgeEntitiesText)>(kPurgeEntitiesText);
/** Compiled form of that pattern; the scan requires one match. */
constexpr auto kGlueMappingPattern =
    signature<signature_length(kGlueMappingText)>(kGlueMappingText);
/** Native reference to the entity record pool. */
constexpr std::string_view kEntityPoolText =
    "48 8D 04 5B 48 0F BF 84 46 14 01 00 00 48 6B D8 70 48 8D 05 ? ? ? ? 48 03 D8 83 7B 48 FF";
/** Native entry that returns an entity's policy. */
constexpr std::string_view kEntityPolicyText =
    "40 55 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 33 "
    "FF 83 FA FF";
/** Compiled forms of those two patterns; the scan requires one match each. */
constexpr auto kEntityPoolPattern = signature<signature_length(kEntityPoolText)>(kEntityPoolText);
constexpr auto kEntityPolicyPattern =
    signature<signature_length(kEntityPolicyText)>(kEntityPolicyText);

/** The five entity signatures, in the order the registry's target list expects them. */
constexpr std::array kEntitySignatures{
    patterns::Pattern{"simulation_sobject_create", kCreateEntityPattern},
    patterns::Pattern{"simulation_entity_purge", kPurgeEntitiesPattern},
    patterns::Pattern{"simulation_glue_mapping", kGlueMappingPattern},
    patterns::Pattern{"simulation_entity_pool", kEntityPoolPattern},
    patterns::Pattern{"simulation_entity_policy", kEntityPolicyPattern},
};

/** The native occupancy mask contains 256 words. */
constexpr std::size_t kEntityMaskWords = 256;
/** The simulation view stores its shared replication epoch at this byte. */
constexpr std::size_t kViewEpochOffset = 53284;
/** Each view index maps to a signed record number in a six-byte row. */
constexpr std::size_t kViewMapOffset = 276, kViewMapStride = 6;
/** Native global records have a fixed stride, pool bound, and view occupancy mask. */
constexpr std::size_t kEntityRecordStride = 112, kEntityRecordCapacity = 1024;
constexpr std::size_t kViewOccupiedOffset = 50464, kEntityFlagsOffset = 80;

struct EntityRecordPrefix final {
    std::uint8_t type{}, lifecycle{};
    std::uint16_t cell{};
    std::uint32_t glue{}, token{}, parent{};
};

/**
 * Logs unselected records without invoking native policy or mutating their state.
 * @param view Simulation view the purge is about to run over.
 * @param mask Selection mask words copied out of the caller's buffer.
 * @param epoch Replication epoch the purge was given.
 */
void report_unselected_records(void* view,
                               const std::array<std::uint32_t, kEntityMaskWords>& mask,
                               std::uint8_t epoch) noexcept {
    for (std::uint32_t slot = 0; g_entityRecordBase != 0 && slot < kPurgeTraceCapacity; ++slot) {
        if ((mask[slot / 32U] & (1U << (slot % 32U))) != 0) {
            continue;
        }
        std::uint32_t occupied = 0;
        if (!memory::read_current_process(
                nullptr,
                reinterpret_cast<std::uintptr_t>(view) + kViewOccupiedOffset
                    + sizeof(occupied) * (slot / 32U),
                std::span(reinterpret_cast<std::byte*>(&occupied), sizeof(occupied)))
            || (occupied & (1U << (slot % 32U))) == 0) {
            continue;
        }
        std::int16_t ordinal = -1;
        if (!memory::read_current_process(
                nullptr,
                reinterpret_cast<std::uintptr_t>(view) + kViewMapOffset + kViewMapStride * slot,
                std::span(reinterpret_cast<std::byte*>(&ordinal), sizeof(ordinal)))
            || ordinal < 0 || static_cast<std::size_t>(ordinal) >= kEntityRecordCapacity) {
            continue;
        }
        EntityRecordPrefix record{};
        std::uint16_t flags = 0;
        const auto address =
            g_entityRecordBase + kEntityRecordStride * static_cast<std::size_t>(ordinal);
        if (!memory::read_current_process(
                nullptr, address, std::span(reinterpret_cast<std::byte*>(&record), sizeof(record)))
            || !memory::read_current_process(
                nullptr,
                address + kEntityFlagsOffset,
                std::span(reinterpret_cast<std::byte*>(&flags), sizeof(flags)))) {
            continue;
        }
        std::array<char, 240> line{};
        const int length =
            std::snprintf(line.data(),
                          line.size(),
                          "ev=world_object stage=entity_unselected epoch=%u slot=%u type=%u "
                          "lifecycle=0x%02X cell=%u flags=0x%04X parent=0x%08X glue=0x%08X",
                          static_cast<unsigned>(epoch),
                          slot,
                          static_cast<unsigned>(record.type),
                          static_cast<unsigned>(record.lifecycle),
                          static_cast<unsigned>(record.cell),
                          static_cast<unsigned>(flags),
                          record.parent,
                          record.glue);
        if (length > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::debug,
                             {line.data(), static_cast<std::size_t>(length)});
        }
    }
}

} // namespace

std::atomic<CreateEntity> g_createEntityOriginal{nullptr};
std::atomic<PurgeEntities> g_purgeEntitiesOriginal{nullptr};
std::atomic<EntityPolicy> g_entityPolicyOriginal{nullptr};
std::uintptr_t g_entityRecordBase{};
std::array<PolicyTrace, kPurgeTraceCapacity> g_policyTrace{};
const std::uintptr_t* g_glueBaseStorage{};
const std::uint32_t* g_glueStrideStorage{};
thread_local std::uint32_t t_entityGlue{kNone};
thread_local std::uint32_t t_entityNetwork{kNone};

/** @return Signatures the entity create, purge and policy hooks need, in resolve order. */
std::span<const patterns::Pattern> entity_patterns() noexcept {
    return kEntitySignatures;
}

/** Carries native entity identity into the allocator trace without changing creation. */
__declspec(noinline) bool __fastcall create_entity(void* definition,
                                                   const void* data,
                                                   std::uint32_t glue,
                                                   std::uint32_t parent) {
    ActiveCall active;
    const auto original = g_createEntityOriginal.load(std::memory_order_acquire);
    const auto savedGlue = t_entityGlue;
    const auto savedNetwork = t_entityNetwork;
    t_entityGlue = glue;
    t_entityNetwork = kNone;
    if (glue != kNone && g_glueBaseStorage != nullptr && g_glueStrideStorage != nullptr) {
        const auto row =
            *g_glueBaseStorage
            + static_cast<std::uintptr_t>(*g_glueStrideStorage) * (glue & kEntityIndexMask);
        static_cast<void>(memory::read_current_process(
            nullptr,
            row,
            std::span(reinterpret_cast<std::byte*>(&t_entityNetwork), sizeof(t_entityNetwork))));
    }
    const bool result = original != nullptr && original(definition, data, glue, parent);
    t_entityGlue = savedGlue;
    t_entityNetwork = savedNetwork;
    return result;
}

/** Reports native policy changes for the bounded glue slots under investigation. */
__declspec(noinline) std::uint32_t __fastcall entity_policy(void* definition, std::uint32_t glue) {
    ActiveCall active;
    const auto original = g_entityPolicyOriginal.load(std::memory_order_acquire);
    const std::uint32_t policy = original != nullptr ? original(definition, glue) : 0;
    if (!g_accepting.load(std::memory_order_acquire) || glue == kNone
        || (glue & kEntityIndexMask) >= kPurgeTraceCapacity || g_glueBaseStorage == nullptr
        || g_glueStrideStorage == nullptr) {
        return policy;
    }
    std::uint32_t token = kNone;
    const auto row =
        *g_glueBaseStorage
        + static_cast<std::uintptr_t>(*g_glueStrideStorage) * (glue & kEntityIndexMask);
    if (!memory::read_current_process(
            nullptr, row, std::span(reinterpret_cast<std::byte*>(&token), sizeof(token)))
        || token == kNone || (token & kEntityIndexMask) >= kPurgeTraceCapacity) {
        return policy;
    }
    AcquireSRWLockExclusive(&g_lock);
    PolicyTrace& previous = g_policyTrace[token & kEntityIndexMask];
    const bool changed = !previous.reported || previous.glue != glue || previous.policy != policy;
    previous = {glue, policy, true};
    ReleaseSRWLockExclusive(&g_lock);
    if (changed) {
        std::array<char, 160> line{};
        const int length = std::snprintf(
            line.data(),
            line.size(),
            "ev=world_object stage=entity_policy slot=%u token=0x%08X glue=0x%08X value=%u",
            token & kEntityIndexMask,
            token,
            glue,
            policy);
        if (length > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::debug,
                             {line.data(), static_cast<std::size_t>(length)});
        }
    }
    return policy;
}

/** Logs the mask the native purge actually consumes and preserves all seven arguments. */
__declspec(noinline) void __fastcall purge_entities(void* view,
                                                    std::int32_t reason,
                                                    const std::uint32_t* mask,
                                                    std::uint32_t* work0,
                                                    std::uint32_t* work1,
                                                    std::uint32_t* work2,
                                                    std::uint8_t epoch) {
    ActiveCall active;
    const auto original = g_purgeEntitiesOriginal.load(std::memory_order_acquire);
    std::array<std::uint32_t, kEntityMaskWords> words{};
    const bool readable = memory::read_current_process(
        nullptr, reinterpret_cast<std::uintptr_t>(mask), std::as_writable_bytes(std::span(words)));
    if (readable && g_accepting.load(std::memory_order_acquire)) {
        report_unselected_records(view, words, epoch);
    }
    std::array<std::uint32_t, kPurgeTraceCapacity> selected{};
    std::array<std::int16_t, kPurgeTraceCapacity> mappedBefore{};
    std::size_t selectedCount = 0;
    for (std::uint32_t slot = 0;
         readable && slot <= kEntityIndexMask && selectedCount < selected.size();
         ++slot) {
        if ((words[slot / 32U] & (1U << (slot % 32U))) == 0) {
            continue;
        }
        selected[selectedCount] = slot;
        mappedBefore[selectedCount] = -1;
        static_cast<void>(memory::read_current_process(
            nullptr,
            reinterpret_cast<std::uintptr_t>(view) + kViewMapOffset + kViewMapStride * slot,
            std::span(reinterpret_cast<std::byte*>(&mappedBefore[selectedCount]),
                      sizeof(std::int16_t))));
        ++selectedCount;
    }
    std::uint8_t before = 0;
    static_cast<void>(memory::read_current_process(
        nullptr,
        reinterpret_cast<std::uintptr_t>(view) + kViewEpochOffset,
        std::span(reinterpret_cast<std::byte*>(&before), sizeof(before))));
    if (readable && g_accepting.load(std::memory_order_acquire)) {
        for (std::size_t word = 0; word < words.size(); ++word) {
            if (words[word] == 0) {
                continue;
            }
            std::array<char, 192> line{};
            const int length = std::snprintf(line.data(),
                                             line.size(),
                                             "ev=world_object stage=entity_purge epoch=%u prior=%u "
                                             "reason=%d word=%zu bits=0x%08X",
                                             static_cast<unsigned>(epoch),
                                             static_cast<unsigned>(before),
                                             reason,
                                             word,
                                             words[word]);
            if (length > 0) {
                core::log::write(core::log::Channel::client,
                                 core::log::Level::debug,
                                 {line.data(), static_cast<std::size_t>(length)});
            }
        }
    }
    if (original != nullptr) {
        original(view, reason, mask, work0, work1, work2, epoch);
    }
    for (std::size_t index = 0;
         index < selectedCount && g_accepting.load(std::memory_order_acquire);
         ++index) {
        std::int16_t mappedAfter = -1;
        static_cast<void>(memory::read_current_process(
            nullptr,
            reinterpret_cast<std::uintptr_t>(view) + kViewMapOffset
                + kViewMapStride * selected[index],
            std::span(reinterpret_cast<std::byte*>(&mappedAfter), sizeof(mappedAfter))));
        std::array<char, 192> line{};
        const int length = std::snprintf(
            line.data(),
            line.size(),
            "ev=world_object stage=entity_purge_slot epoch=%u slot=%u before=%d after=%d view=%p",
            static_cast<unsigned>(epoch),
            selected[index],
            static_cast<int>(mappedBefore[index]),
            static_cast<int>(mappedAfter),
            view);
        if (length > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::debug,
                             {line.data(), static_cast<std::size_t>(length)});
        }
    }
    std::uint8_t after = before;
    static_cast<void>(memory::read_current_process(
        nullptr,
        reinterpret_cast<std::uintptr_t>(view) + kViewEpochOffset,
        std::span(reinterpret_cast<std::byte*>(&after), sizeof(after))));
    if (g_accepting.load(std::memory_order_acquire)) {
        std::array<char, 160> line{};
        const int length = std::snprintf(
            line.data(),
            line.size(),
            "ev=world_object stage=entity_purge result=returned epoch=%u current=%u readable=%u",
            static_cast<unsigned>(epoch),
            static_cast<unsigned>(after),
            static_cast<unsigned>(readable));
        if (length > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::debug,
                             {line.data(), static_cast<std::size_t>(length)});
        }
    }
}

} // namespace sunrise::client::hooks::world_objects
