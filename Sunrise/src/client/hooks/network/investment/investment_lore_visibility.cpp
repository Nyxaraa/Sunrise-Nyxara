#include <Windows.h>

#include <array>
#include <limits>

#include "../../../../core/logging/log.h"
#include "../../../content/handles/handle_resolver.h"
#include "../../../memory/current_process_memory.h"
#include "../../../targets/game/content.h"
#include "internal.h"
#include "lore_visibility_patch.h"

namespace sunrise::client::hooks::network::investment {
namespace {
struct Descriptor {
    std::uint64_t count;
    std::int64_t relative;
};
struct Patch {
    std::uintptr_t address;
    lore::Instruction before, after;
};
std::array<Patch, lore::kTargets.size()> g_patches{};
std::size_t g_count{};
SRWLOCK g_lock = SRWLOCK_INIT;

template <class T> bool read(std::uintptr_t address, T& value) noexcept {
    return memory::read_current_process(
        nullptr, address, std::as_writable_bytes(std::span(&value, 1)));
}
bool data_at(std::uintptr_t address, const Descriptor& desc, std::uintptr_t& data) noexcept {
    if (address > static_cast<std::uintptr_t>(INT64_MAX) - 24) return false;
    const auto base = static_cast<std::int64_t>(address) + 8;
    if (desc.relative < -base || desc.relative > INT64_MAX - base - 16) return false;
    const auto header = static_cast<std::uintptr_t>(base + desc.relative);
    std::uint64_t count{};
    std::uint32_t marker{}, type{};
    if (header < 4 || !read(header, count) || count != desc.count || !read(header - 4, marker)
        || !read(header + 8, type) || marker >> 16 != 0x8080 || type >> 16 != 0x8080)
        return false;
    data = header + 16;
    return true;
}
bool write(const Patch& patch, bool restore) noexcept {
    const auto expected = restore ? patch.after : patch.before;
    const auto desired = restore ? patch.before : patch.after;
    lore::Instruction current{};
    if (!read(patch.address, current) || current != expected) return false;
    auto* destination = reinterpret_cast<void*>(patch.address);
    DWORD previous{};
    if (!VirtualProtect(destination, sizeof desired, PAGE_READWRITE, &previous)) return false;
    SIZE_T written{};
    const bool copied =
        WriteProcessMemory(GetCurrentProcess(), destination, &desired, sizeof desired, &written)
        && written == sizeof desired;
    DWORD ignored{};
    const bool protectedAgain =
        VirtualProtect(destination, sizeof desired, previous, &ignored) != FALSE;
    return copied && protectedAgain && read(patch.address, current) && current == desired;
}
bool rollback() noexcept {
    bool ok = true;
    for (std::size_t i = g_count; i > 0; --i) {
        lore::Instruction current{};
        if (!read(g_patches[i - 1].address, current)) {
            ok = false;
            continue;
        }
        if (current == g_patches[i - 1].before) continue;
        if (!write(g_patches[i - 1], true)) ok = false;
    }
    if (ok) g_count = 0;
    return ok;
}
bool prepare(std::array<Patch, lore::kTargets.size()>& staged) noexcept {
    content::handles::Source source{};
    source.tablesSlot =
        reinterpret_cast<std::uintptr_t>(targets::game::content::get().contentHandleTablesSlot);
    source.read = &memory::read_current_process;
    std::array<std::uintptr_t, 2> rows{};
    constexpr std::array<std::uint32_t, 2> tags{0x81319339U, 0x8131933FU};
    constexpr std::array<std::size_t, 2> counts{2242, 924}, strides{216, 168};
    for (std::size_t i = 0; i < 2; ++i) {
        std::uintptr_t table{};
        Descriptor desc{};
        if (!content::handles::resolve(source, tags[i], table) || !read(table + 8, desc)
            || desc.count != counts[i] || !data_at(table + 8, desc, rows[i]))
            return false;
    }
    for (std::size_t i = 0; i < staged.size(); ++i) {
        const auto& target = lore::kTargets[i];
        const std::size_t kind = target.node ? 1 : 0;
        const auto row = rows[kind] + target.row * strides[kind];
        std::uint32_t hash{};
        Descriptor desc{};
        std::uintptr_t data{};
        std::array<lore::Instruction, 59> code{};
        if (!read(row + 40, hash) || hash != target.hash || !read(row + target.field, desc)
            || desc.count == 0 || desc.count > code.size()
            || !data_at(row + target.field, desc, data)
            || !memory::read_current_process(
                nullptr,
                data,
                std::as_writable_bytes(std::span(code).first(static_cast<std::size_t>(desc.count))))
            || !lore::replacement(target.shape,
                                  std::span(code).first(static_cast<std::size_t>(desc.count)),
                                  staged[i].after))
            return false;
        staged[i].address = data;
        staged[i].before = code[0];
    }
    // Each edited instruction must be owned by exactly one presentation condition. Never mutate
    // a constant shared with another record, even if that record is not in this repair's list.
    std::array<unsigned, lore::kTargets.size()> references{};
    for (std::size_t kind = 0; kind < 2; ++kind) {
        for (std::size_t row = 0; row < counts[kind]; ++row) {
            for (const std::size_t field : (kind == 0 ? std::array<std::size_t, 2>{120, 136}
                                                      : std::array<std::size_t, 2>{48, 64})) {
                const auto at = rows[kind] + row * strides[kind] + field;
                Descriptor desc{};
                std::uintptr_t data{};
                if (!read(at, desc)) return false;
                if (desc.count == 0) continue;
                if (desc.count > 128 || !data_at(at, desc, data)) return false;
                for (std::size_t i = 0; i < staged.size(); ++i)
                    if (staged[i].address >= data && staged[i].address - data < desc.count * 8)
                        ++references[i];
            }
        }
    }
    for (auto count : references)
        if (count != 1) return false;
    return true;
}
} // namespace

void apply_lore_visibility() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_count != 0) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    std::array<Patch, lore::kTargets.size()> staged{};
    bool ok = prepare(staged);
    if (ok) {
        for (const auto& patch : staged) {
            g_patches[g_count++] = patch;
            if (!write(patch, false)) {
                ok = false;
                break;
            }
        }
    }
    const bool restored = ok || rollback();
    core::log::write(core::log::Channel::client,
                     ok ? core::log::Level::info : core::log::Level::warn,
                     ok ? "ev=lore_visibility result=applied conditions=36 progress_unchanged=1"
                     : restored ? "ev=lore_visibility result=refused originals_retained=1"
                                : "ev=lore_visibility result=rollback_failed");
    ReleaseSRWLockExclusive(&g_lock);
}
void restore_lore_visibility() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (!rollback())
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=lore_visibility result=restore_failed");
    ReleaseSRWLockExclusive(&g_lock);
}
} // namespace sunrise::client::hooks::network::investment
