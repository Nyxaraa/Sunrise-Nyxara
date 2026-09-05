#include "retail_log_enqueue_observer.h"

#include <intrin.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../bootflow/bootflow_hook_lifecycle.h"
#include "../network/investment/internal.h"
#include "../../targets/game.h"

namespace sunrise::client::hooks::retail_log {
namespace {

using Enqueue = void(__fastcall*)(std::int32_t, const char*) noexcept;
using SetCategoryVerbosity = void(__fastcall*)(std::int32_t, std::uint32_t) noexcept;

/** The game copies exactly this many bytes out of the caller's text buffer. */
constexpr std::size_t kNativeTextSize = 320;
/** Site id the game uses for an unregistered line. */
constexpr std::int32_t kUnregisteredSite = -1;
/** Line storage holds the cleaned text plus its fixed key prefix. */
constexpr std::size_t kEventCapacity = kNativeTextSize + 64;
/** A late config load resets the thresholds, so set them again on this period. A count will not
 *  do: a closed category emits fewer lines, so it advances slower and stays closed. */
constexpr std::uint64_t kReassertIntervalMs = 2'000;
/** How many categories the game's own verbosity table holds. */
constexpr std::uint32_t kCategoryCount = 26;
/** 0 is the game's loosest category threshold. A higher value logs less. */
constexpr std::uint32_t kMostVerbose = 0;
/** Native boundary after plug tables finish patching and before their derived views resume. */
constexpr std::string_view kContentTablePatchingComplete =
    "content_table_patching: patch contents have been cleared";

thread_local bool g_inObserver{};
/** Tick at which the next re-assert is due. Zero makes the first call assert. */
volatile LONG64 g_nextAssertTick{};

/**
 * Copies the native text into fixed storage as one printable line.
 * @param text Borrowed native buffer.
 * @param output Receives the cleaned characters.
 * @return Number of characters written.
 */
[[nodiscard]] std::size_t sanitize(const char* text, std::array<char, kNativeTextSize>& output) {
    std::size_t length = 0;
    __try {
        for (; length < kNativeTextSize - 1 && text[length] != '\0'; ++length) {
            const char value = text[length];
            // One line, one event: the native text carries its own line breaks.
            output[length] = value >= ' ' && value != '\x7F' ? value : ' ';
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    while (length != 0 && output[length - 1] == ' ') {
        --length;
    }
    return length;
}

/**
 * Writes one captured line.
 * @param siteId Registered site id.
 * @param text Borrowed native buffer.
 */
void capture_line(std::int32_t siteId, const char* text) noexcept {
    std::array<char, kNativeTextSize> sanitized{};
    const std::size_t textLength = sanitize(text, sanitized);
    const std::string_view message{sanitized.data(), textLength};
    if (message.find(kContentTablePatchingComplete) != std::string_view::npos) {
        network::investment::arm_socket_menu_routing();
        // This must happen before the native logger returns to investment initialization. A later
        // callback tick races the socket-menu caches that consume these descriptors.
        network::investment::apply_socket_menu_routing();
        network::investment::apply_lore_visibility();
    }
    if (!core::log::accepts(core::log::Channel::client, core::log::Level::info)) {
        return;
    }
    std::array<char, kEventCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=retail site=%d text=%.*s",
                                      siteId,
                                      static_cast<int>(textLength),
                                      sanitized.data());
    if (written <= 0) {
        return;
    }
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(core::log::Channel::client, core::log::Level::info, {line.data(), length});
}

/** Text whose emitting call site is worth locating in the image. */
constexpr std::string_view kTracedText = "failed to create";
/** Call sites named per run, so a repeating line cannot flood the sink. */
constexpr std::size_t kMaxCallSiteReports = 64;

/** Reports already spent. */
volatile LONG g_callSiteReports{};

/**
 * Names the image offset of the code that emitted one line.
 * The packed executable cannot be disassembled on disk, so a dump of the mapped image is the only
 * readable copy, and an offset from the load base is what addresses it. The retail text itself
 * carries no address, and the site id is assigned by the game's own registration rather than by
 * position, so nothing else here says which function produced a line. `_ReturnAddress` inside the
 * funnel is the emitting call site, which is exactly the function to disassemble.
 * @param returnAddress Return address captured in the funnel.
 * @param text Already-formatted native line.
 */
void report_call_site(const void* returnAddress, const char* text) noexcept {
    if (returnAddress == nullptr
        || !core::log::accepts(core::log::Channel::client, core::log::Level::debug)) {
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto site = reinterpret_cast<std::uintptr_t>(returnAddress);
    if (base == 0 || site < base) {
        return;
    }
    if (InterlockedIncrement(&g_callSiteReports) > static_cast<LONG>(kMaxCallSiteReports)) {
        return;
    }
    std::array<char, kEventCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=retail_site stage=caller rva=0x%llX va=0x%llX text=%s",
                                      static_cast<unsigned long long>(site - base),
                                      static_cast<unsigned long long>(site),
                                      text);
    if (written > 0) {
        const auto length = static_cast<std::size_t>(written) < line.size()
                                ? static_cast<std::size_t>(written)
                                : line.size() - 1;
        core::log::write(core::log::Channel::client, core::log::Level::debug,
                         {line.data(), length});
    }
}

/**
 * Mirrors the single funnel every retail log line passes through.
 * @param siteId Registered site id.
 * @param text Native buffer holding the already-formatted line.
 */
__declspec(noinline) void __fastcall enqueue_body(std::int32_t siteId, const char* text) noexcept {
    // The verbosity setter logs through this same funnel; without this it would recurse.
    const bool outer = !g_inObserver;
    g_inObserver = true;
    const auto call = reinterpret_cast<Enqueue>(g_handle.original);
    if (call != nullptr) {
        call(siteId, text);
    }
    if (outer) {
        if (siteId != kUnregisteredSite && text != nullptr) {
            capture_line(siteId, text);
            // Cheap guard first: the search only runs on the handful of lines that match.
            if (std::string_view(text).find(kTracedText) != std::string_view::npos) {
                report_call_site(_ReturnAddress(), text);
            }
        }
        assert_verbosity();
        g_inObserver = false;
    }
}

} // namespace

/** @return The enqueue observer body itself, with internal linkage. */
void* enqueue_entry_point() noexcept {
    return reinterpret_cast<void*>(&enqueue_body);
}

/**
 * Opens every category in the game's own log table, once we know the block exists. Reaching the
 * enqueue funnel is the proof: without the block the native body returns early.
 */
void assert_verbosity() noexcept {
    // How much the game logs follows the client threshold, so debug is what opens its table.
    if (!core::log::accepts(core::log::Channel::client, core::log::Level::debug)) {
        return;
    }
    const auto now = static_cast<LONG64>(GetTickCount64());
    const LONG64 due = g_nextAssertTick;
    if (now < due) {
        return;
    }
    // One claim per period, so concurrent funnel threads do not all reopen the table.
    if (InterlockedCompareExchange64(
            &g_nextAssertTick, now + static_cast<LONG64>(kReassertIntervalMs), due)
        != due) {
        return;
    }
    const auto setter = reinterpret_cast<SetCategoryVerbosity>(
        targets::game::retail_log::get().setCategoryVerbosity);
    if (setter == nullptr) {
        return;
    }
    for (std::uint32_t category = 0; category < kCategoryCount; ++category) {
        setter(static_cast<std::int32_t>(category), kMostVerbose);
    }
}

} // namespace sunrise::client::hooks::retail_log
