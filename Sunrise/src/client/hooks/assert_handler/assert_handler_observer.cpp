#include "assert_handler_observer.h"

#include <Windows.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "../../../core/logging/log.h"
#include "../../targets/game/assert_handler.h"
#include "../net_tick_probe/net_tick_probe.h"
#include "core/threading/srw_lock.h"

namespace sunrise::client::hooks::assert_handler {
namespace {

/** The game formats assert text into a buffer of this size, so it bounds ours too. */
constexpr std::size_t kTextCapacity = 1024;
/** One log line carries the message plus its fixed prefix. */
constexpr std::size_t kLineCapacity = 1152;
/** Consecutive repeats of one message written in full before counting takes over. */
constexpr std::uint32_t kRepeatHead = 8;
/** One repeat in this many is written after that, so a stuck assert never goes silent. */
constexpr std::uint32_t kRepeatStride = 512;
/** Used when the game's own format string cannot be printed. */
constexpr char kUnformattable[] = "<unformattable>";

/**
 * Halt category of the graphics device-loss assert. A removed device never returns, so this one
 * must keep halting the process. Every other category stays non-fatal.
 */
constexpr int kGraphicsHaltCategory = 6;

/** The handler the game installed, called with the same printf-style arguments the sites use. */
using NativeHandler = void(__cdecl*)(int, const char*, ...);

core::threading::SrwLock g_lock{};
/** Last message seen, so a message that repeats every frame is counted rather than written. */
std::array<char, kTextCapacity> g_lastText{};
std::uint32_t g_repeats{};
std::uint32_t g_seen{};

/** @param code Halt category from the assert site. @return True when the process must still die. */
[[nodiscard]] bool unrecoverable(int code) noexcept {
    return code == kGraphicsHaltCategory;
}

/**
 * Counts one message and says whether this occurrence is written.
 * @param text Formatted assert message.
 * @param seen Receives the total assert count.
 * @param repeats Receives the length of the current run of this message.
 * @return True when the caller writes a log line.
 */
[[nodiscard]] bool admit(const char* text, std::uint32_t& seen, std::uint32_t& repeats) noexcept {
    const std::lock_guard lock(g_lock);
    ++g_seen;
    if (std::strcmp(g_lastText.data(), text) == 0) {
        ++g_repeats;
    } else {
        const std::size_t length = std::strlen(text);
        const std::size_t copied = length < kTextCapacity ? length : kTextCapacity - 1;
        std::memcpy(g_lastText.data(), text, copied);
        g_lastText[copied] = '\0';
        g_repeats = 1;
    }
    seen = g_seen;
    repeats = g_repeats;
    return repeats <= kRepeatHead || repeats % kRepeatStride == 0;
}

/**
 * Records one assert without letting logging failures reach the game.
 * @param code Halt category the assert site passes. Zero, one and six all occur.
 * @param text Already-formatted assert message.
 */
void report(int code, const char* text) noexcept {
    std::uint32_t seen = 0;
    std::uint32_t repeats = 0;
    if (!admit(text, seen, repeats)) {
        return;
    }
    // A repeating assert is the one thing still running when the game's networking tick stops, so
    // the latch that gates that tick is read from here. The probe throttles itself.
    net_tick_probe::sample();
    std::array<char, kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=assert stage=hit n=%u repeat=%u arg0=%d text=%s",
                                      seen,
                                      repeats,
                                      code,
                                      text);
    if (written <= 0) {
        return;
    }
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(core::log::Channel::client, core::log::Level::error, {line.data(), length});
}

/**
 * Hands one assert back to the handler the game installed, which halts the process.
 * @param code Halt category from the assert site.
 * @param text Message this handler already formatted.
 */
void chain(int code, const char* text) noexcept {
    if (!targets::game::assert_handler::is_resolved()) {
        return;
    }
    const targets::game::assert_handler::Targets& resolved = targets::game::assert_handler::get();
    if (resolved.original == nullptr) {
        return;
    }
    std::array<char, 64> line{};
    const int written =
        std::snprintf(line.data(), line.size(), "ev=assert stage=halt arg0=%d result=native", code);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    // The original is printf-style too, so the text already built is passed as its one argument.
    reinterpret_cast<NativeHandler>(resolved.original)(code, "%s", text);
}

/**
 * Replacement assert handler. The sites call this slot as a printf-style callback. Returning
 * without calling the game's own handler is what makes the assert non-fatal. That handler builds
 * a crash ticket, shows a dialog and blocks.
 * @param code Halt category from the assert site.
 * @param format Native printf-style format string.
 */
void __cdecl handler_body(int code, const char* format, ...) noexcept {
    std::array<char, kTextCapacity> text{};
    if (format == nullptr) {
        std::memcpy(text.data(), kUnformattable, sizeof kUnformattable);
    } else {
        va_list arguments;
        va_start(arguments, format);
        const int written = std::vsnprintf(text.data(), text.size(), format, arguments);
        va_end(arguments);
        if (written < 0) {
            std::memcpy(text.data(), kUnformattable, sizeof kUnformattable);
        }
    }
    text.back() = '\0';

    // An assert raised by our own logging must not re-enter this body.
    static thread_local bool inside = false;
    if (inside) {
        return;
    }
    inside = true;
    report(code, text.data());
    // Swallowing an unrecoverable halt leaves the process running with nothing to present. The
    // original handler runs so the process dies the way the game intends.
    if (unrecoverable(code)) {
        chain(code, text.data());
    }
    inside = false;
}

} // namespace

/** @return Address of the internal assert handler body. */
void* handler_entry_point() noexcept {
    return reinterpret_cast<void*>(&handler_body);
}

} // namespace sunrise::client::hooks::assert_handler
