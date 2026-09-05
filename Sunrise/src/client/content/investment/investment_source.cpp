#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "../../../core/logging/log.h"
#include "../../../state/content/content_catalog.h"
#include "../../memory/current_process_memory.h"
#include "../../targets/game/content.h"
#include "../handles/layout.h"
#include "internal.h"
#include "layout.h"

namespace sunrise::client::content::investment {
namespace {

/** FNV-1 hash of the investment-globals bootstrap name, so the name itself is not shipped. */
constexpr std::uint32_t kInvestmentGlobalsNameHash = 0x6F7125CBU;
/** The bootstrap name is not unique, so every match is collected. */
// Keep this identical to the package extractor. The installed catalogue currently has more than
// eight entries with this shared name; treating a truncated lookup as total failure made live
// socket routing permanently defer even though the correct candidate was present.
constexpr std::size_t kBootstrapMatchCapacity = 64;
/** One native resolver prefix is enough to recover all descriptor field offsets. */
constexpr std::size_t kResolverDiagnosticBytes = 128;
/** Package handles name descriptor ids 1,024 slots above their package id. */
constexpr std::uintptr_t kContentDescriptorBias = 1024;
/** Package handles keep their package id above thirteen entry-index bits. */
constexpr unsigned kPackageShift = 13;
/** Installed definition tags begin at this package-handle base. */
constexpr std::uint32_t kPackageTagBase = 0x80800000U;

std::atomic_bool g_diagnosticsReported{false};

/** Reads one complete scalar through the bounded live-process reader. */
template <typename Value>
[[nodiscard]] bool read(const Source& source, std::uintptr_t address, Value& value) noexcept {
    return source.handles.read != nullptr
           && source.handles.read(source.handles.context,
                                  address,
                                  std::span(reinterpret_cast<std::byte*>(&value), sizeof value));
}

/** Reads one scalar directly from the current process for layout diagnostics. */
template <typename Value>
[[nodiscard]] bool read_process(std::uintptr_t address, Value& value) noexcept {
    return memory::read_current_process(
        nullptr, address, std::span(reinterpret_cast<std::byte*>(&value), sizeof value));
}

/** Writes one bounded memory range as a single diagnostic line. */
void report_bytes(const char* stage,
                  std::uintptr_t address,
                  std::span<const std::byte> bytes) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int prefix = std::snprintf(line.data(),
                                     line.size(),
                                     "ev=investment stage=%s address=0x%llX bytes=",
                                     stage,
                                     static_cast<unsigned long long>(address));
    if (prefix <= 0) {
        return;
    }
    std::size_t length = (std::min)(static_cast<std::size_t>(prefix), line.size() - 1U);
    static_cast<void>(core::log::append_hex(line, length, bytes));
    core::log::write(core::log::Channel::client, core::log::Level::warn, {line.data(), length});
}

/** Writes one candidate descriptor and its package identity as a diagnostic line. */
void report_descriptor(unsigned depth,
                       const state::content::Definition& candidate,
                       std::uintptr_t address,
                       std::span<const std::byte> bytes) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int prefix = std::snprintf(line.data(),
                                     line.size(),
                                     "ev=investment stage=descriptor depth=%u tag=0x%08X "
                                     "class=0x%08X address=0x%llX bytes=",
                                     depth,
                                     candidate.tag,
                                     candidate.classId,
                                     static_cast<unsigned long long>(address));
    if (prefix <= 0) {
        return;
    }
    std::size_t length = (std::min)(static_cast<std::size_t>(prefix), line.size() - 1U);
    static_cast<void>(core::log::append_hex(line, length, bytes));
    core::log::write(core::log::Channel::client, core::log::Level::warn, {line.data(), length});
}

/** Captures the native resolver and every plausible descriptor base once, without mutation. */
void report_layout_diagnostics(const targets::game::content::Targets& targets,
                               std::span<const state::content::Definition> candidates) noexcept {
    if (g_diagnosticsReported.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    std::array<std::byte, kResolverDiagnosticBytes> resolver{};
    if (targets.queuezObjectResolver != nullptr
        && memory::read_current_process(
            nullptr, reinterpret_cast<std::uintptr_t>(targets.queuezObjectResolver), resolver)) {
        report_bytes("resolver_bytes",
                     reinterpret_cast<std::uintptr_t>(targets.queuezObjectResolver),
                     resolver);
    }

    const std::uintptr_t slot = reinterpret_cast<std::uintptr_t>(targets.contentHandleTablesSlot);
    std::array<std::uintptr_t, 3> bases{};
    const bool base0 = read_process(slot, bases[0]);
    const bool base1 = base0 && bases[0] != 0 && read_process(bases[0], bases[1]);
    const bool base2 = base1 && bases[1] != 0 && read_process(bases[1], bases[2]);
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=investment stage=table_chain slot=0x%llX "
                                      "read=%u/%u/%u base=0x%llX/0x%llX/0x%llX",
                                      static_cast<unsigned long long>(slot),
                                      base0 ? 1U : 0U,
                                      base1 ? 1U : 0U,
                                      base2 ? 1U : 0U,
                                      static_cast<unsigned long long>(bases[0]),
                                      static_cast<unsigned long long>(bases[1]),
                                      static_cast<unsigned long long>(bases[2]));
    if (written > 0) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::warn,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }

    for (unsigned depth = 0; depth < bases.size(); ++depth) {
        if (bases[depth] == 0) {
            continue;
        }
        for (const state::content::Definition& candidate : candidates) {
            if (candidate.tag < kPackageTagBase) {
                continue;
            }
            const std::uintptr_t package =
                static_cast<std::uintptr_t>(candidate.tag - kPackageTagBase) >> kPackageShift;
            const std::uintptr_t descriptor =
                bases[depth]
                + (package + kContentDescriptorBias) * handles::layout::kTableDescriptorSize;
            std::array<std::byte, handles::layout::kTableDescriptorSize> bytes{};
            if (memory::read_current_process(nullptr, descriptor, bytes)) {
                report_descriptor(depth, candidate, descriptor, bytes);
            }
        }
    }
}

} // namespace

/** Resolves the checked live investment source selected by its installed bootstrap name. */
bool resolve_source(Source& source) noexcept {
    source = {};
    const auto& runtimeTargets = targets::game::content::get();
    if (!targets::game::content::is_resolved()
        || runtimeTargets.contentHandleTablesSlot == nullptr) {
        return false;
    }

    std::array<state::content::Definition, kBootstrapMatchCapacity> candidates{};
    std::size_t count = 0;
    // A shared bootstrap name may have more installed matches than this bounded scratch array.
    // Truncation is not a lookup failure: every copied candidate is still safe to validate, and
    // rejecting the whole set is what kept socket-category routing permanently deferred.
    if (!state::content::lookup_hash(kInvestmentGlobalsNameHash, candidates, count) && count == 0) {
        return false;
    }
    report_layout_diagnostics(runtimeTargets, std::span(candidates).first(count));
    std::size_t globalsResolved = 0;
    std::size_t rootTagsRead = 0;
    std::size_t rootsResolved = 0;
    std::size_t tableTagsRead = 0;
    std::size_t tablesResolved = 0;
    for (std::size_t index = 0; index < count; ++index) {
        Source candidate{};
        candidate.investmentGlobalsTag = candidates[index].tag;
        candidate.handles.tablesSlot =
            reinterpret_cast<std::uintptr_t>(runtimeTargets.contentHandleTablesSlot);
        candidate.handles.read = &memory::read_current_process;

        std::uintptr_t globals = 0;
        std::uintptr_t root = 0;
        std::uintptr_t table = 0;
        std::uint32_t rootTag = 0;
        std::uint32_t tableTag = 0;
        std::uint64_t rowCount = 0;
        if (!handles::resolve(candidate.handles, candidate.investmentGlobalsTag, globals)) {
            continue;
        }
        ++globalsResolved;
        if (!read(candidate, globals + layout::kGlobalsRootTagOffset, rootTag)) {
            continue;
        }
        ++rootTagsRead;
        if (!handles::resolve(candidate.handles, rootTag, root)) {
            continue;
        }
        ++rootsResolved;
        if (!read(candidate, root + layout::kItemTableTagOffset, tableTag)) {
            continue;
        }
        ++tableTagsRead;
        if (!handles::resolve(candidate.handles, tableTag, table)) {
            continue;
        }
        ++tablesResolved;
        if (!read(candidate, table + 8U, rowCount) || rowCount == 0 || rowCount > 32768U) {
            continue;
        }
        source = candidate;
        return true;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=investment stage=source result=deferred candidates=%llu "
                                      "globals=%llu root_tags=%llu roots=%llu table_tags=%llu "
                                      "tables=%llu",
                                      static_cast<unsigned long long>(count),
                                      static_cast<unsigned long long>(globalsResolved),
                                      static_cast<unsigned long long>(rootTagsRead),
                                      static_cast<unsigned long long>(rootsResolved),
                                      static_cast<unsigned long long>(tableTagsRead),
                                      static_cast<unsigned long long>(tablesResolved));
    if (written > 0) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::warn,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
    return false;
}

} // namespace sunrise::client::content::investment
