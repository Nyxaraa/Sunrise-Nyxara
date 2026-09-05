#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <intrin.h>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../../../state/activity/forced/activity_forced_destination.h"
#include "../../hooking/detour.h"
#include "internal.h"

namespace sunrise::client::hooks::bootflow {
namespace {

/**
 * The bubble public-flag reader. The pattern is its whole body: a call to the state-byte getter,
 * then a cmovnz that turns the byte into a bool.
 */
constexpr std::string_view kReaderSignatureText =
    "48 83 EC 28 E8 ? ? ? ? 48 8B C8 32 C0 48 85 C9 74 ? 80 39 00 BA 01 00 00 00 0F B6 C0 0F 45 "
    "C2 48 83 C4 28 C3";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kReaderSignature =
    signature<signature_length(kReaderSignatureText)>(kReaderSignatureText);

/**
 * The region transition starter. Anchored on its stack-cookie prologue and the read of the
 * manager's phase byte, which no other function pairs this way.
 */
constexpr std::string_view kStarterSignatureText =
    "44 89 44 24 18 55 53 56 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 "
    "? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 0F B6 81 09 02 00 00 4D 8B E1 FE C8 4C 63 F2 48 8B F1";
/** Compiled pattern bytes of the signature text above. */
constexpr auto kStarterSignature =
    signature<signature_length(kStarterSignatureText)>(kStarterSignatureText);

/** `call rel32`, the encoding the starter uses to reach the reader. */
constexpr std::byte kCallOpcode{0xE8};
/** The call's displacement follows its opcode byte. */
constexpr std::size_t kCallOperandOffset = 1;
/** A near call is its opcode plus a signed 32-bit displacement. */
constexpr std::size_t kCallLength = kCallOperandOffset + 4;
/**
 * Bytes of the starter searched for that call. The body is shorter than this, and the search
 * needs one match, so a second hit fails the install instead of picking one.
 */
constexpr std::size_t kStarterSearchBytes = 0x600;

/** Lines allowed per run. Region transitions are rare, so this shows every one a boot makes. */
constexpr unsigned kMaxReports = 8;
/** Size of one line, set by its stage and slice-set fields. */
constexpr std::size_t kLineCapacity = 96;

using Reader = bool(__fastcall*)(std::uint32_t);

hooking::detour::Handle g_handle{};
std::atomic<Reader> g_original{nullptr};
std::atomic<const std::byte*> g_returnSite{nullptr};
std::atomic<unsigned> g_forced{0};

/**
 * Finds the return address of the starter's own call to the reader.
 * A stray opcode byte inside another instruction can decode to the reader, so the whole window is
 * swept and an unclear result is rejected.
 * @return Address after the single matching call, or null when there is not exactly one.
 */
[[nodiscard]] const std::byte* find_return_site(const std::byte* starter,
                                                const std::byte* reader) noexcept {
    const std::byte* found = nullptr;
    for (std::size_t offset = 0; offset + kCallLength <= kStarterSearchBytes; ++offset) {
        const std::byte* const site = starter + offset;
        if (*site != kCallOpcode) {
            continue;
        }
        const std::byte* const next = site + kCallLength;
        if (resolve_relative(site + kCallOperandOffset, next) != reader) {
            continue;
        }
        if (found != nullptr) {
            return nullptr;
        }
        found = next;
    }
    return found;
}

/**
 * Emits one decision event while the per-run budget lasts. Only a public bubble reaches here.
 * @param sliceSet Slice-set index whose bubble the reader called public.
 * @param forced True when the answer was replaced, false when the region stays public.
 */
void report(std::uint32_t sliceSet, bool forced) noexcept {
    // One atomic claim per line, so a concurrent transition cannot reuse a budget slot.
    if (g_forced.fetch_add(1, std::memory_order_relaxed) >= kMaxReports) {
        return;
    }
    std::array<char, kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=bootflow stage=region result=%s slice_set=%u",
                                      forced ? "forced" : "public",
                                      static_cast<unsigned>(sliceSet));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Reports a bubble as private, for the region transition's own call only.
 * A public region holds its slice-set switch until a public activity host connects. The answer
 * is public unless `client.region_private` is on, or a destination is forced.
 * @return False on the starter's call, otherwise the reader's own answer.
 */
__declspec(noinline) bool __fastcall reader(std::uint32_t sliceSet) noexcept {
    const Reader original = g_original.load(std::memory_order_acquire);
    // The detour is live for a few instructions before install publishes its trampoline.
    if (original == nullptr) {
        return false;
    }
    if (!original(sliceSet)) {
        return false;
    }
    const auto* const caller = static_cast<const std::byte*>(_ReturnAddress());
    if (caller != g_returnSite.load(std::memory_order_acquire)) {
        return true;
    }
    const core::settings::Settings& settings = core::settings::get();
    const bool forced = settings.client.regionPrivate || state::activity::forced::override_active();
    report(sliceSet, forced);
    return !forced;
}

/** @param reason Key naming the step that failed. */
void report_failure(const char* reason) noexcept {
    std::array<char, kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=bootflow stage=region result=fail reason=%s", reason);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

/** Stages the private-region force. */
StageResult stage_region_private(hooking::detour::Spec& spec) noexcept {
    if (g_handle.attached) {
        return StageResult::attached;
    }
    std::byte* const target = scan_main_image_unique(kReaderSignature, "slice_set_is_public");
    if (target == nullptr) {
        report_failure("reader");
        return StageResult::unavailable;
    }
    const std::byte* const starter =
        scan_main_image_unique(kStarterSignature, "region_start_transition");
    if (starter == nullptr) {
        report_failure("starter");
        return StageResult::unavailable;
    }
    const std::byte* const returnSite = find_return_site(starter, target);
    if (returnSite == nullptr) {
        report_failure("call_site");
        return StageResult::unavailable;
    }
    // Published before the detour attaches, so the first call already has its filter.
    g_returnSite.store(returnSite, std::memory_order_release);
    spec = hooking::detour::Spec{target, reinterpret_cast<void*>(&reader)};
    return StageResult::staged;
}

/** Takes the private-region force's attached handle, or a detached one. */
void publish_region_private(const hooking::detour::Handle& handle) noexcept {
    if (!handle.attached) {
        report_failure("attach");
        return;
    }
    g_handle = handle;
    g_original.store(reinterpret_cast<Reader>(g_handle.original), std::memory_order_release);
    core::log::write(
        core::log::Channel::client, core::log::Level::info, "ev=bootflow stage=region result=ok");
}

/** Detaches the private-region force. */
void uninstall_region_private() noexcept {
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_original.store(nullptr, std::memory_order_release);
    g_returnSite.store(nullptr, std::memory_order_release);
    g_forced.store(0, std::memory_order_release);
}

} // namespace sunrise::client::hooks::bootflow
