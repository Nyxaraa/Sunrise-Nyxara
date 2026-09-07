#include "entity_position_profiles.h"

#include <Windows.h>

#include <algorithm>
#include <span>

namespace sunrise::state::gameplay::entity_position_profiles {
namespace {
SRWLOCK g_lock{SRWLOCK_INIT};
Rows g_rows;
Fingerprint g_fingerprint{};
bool g_confirmed{};
// An activity name is stored null terminated, so one byte of the capacity is reserved.
constexpr std::size_t kMaximumName = kNameCapacity - 1;
} // namespace
/** Only sorted, unique activity/cell records enter the immutable catalogue. */
bool validate(std::span<const Row> rows) noexcept {
    if (rows.empty() || rows.size() > kMaximumRows) {
        return false;
    }
    const Row* previous = nullptr;
    for (const auto& row : rows) {
        const std::string_view activity = row.activity.view();
        if (activity.empty() || activity.size() > kMaximumName || row.cell > 255 || row.bubble >= 64
            || activity.find('\0') != std::string_view::npos
            || std::any_of(
                row.axisBits.begin(), row.axisBits.end(), [](auto width) { return width > 31; })) {
            return false;
        }
        if (previous
            && (previous->activity > row.activity
                || (previous->activity == row.activity && previous->cell >= row.cell))) {
            return false;
        }
        previous = &row;
    }
    return true;
}
/** Publication swaps already-validated storage while readers hold the shared lock. */
bool publish(Rows rows, const Fingerprint& fingerprint) noexcept {
    if (!validate(rows)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_rows.swap(rows);
    g_fingerprint = fingerprint;
    g_confirmed = true;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}
bool ready(const Fingerprint& fingerprint) noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool result = g_confirmed && !g_rows.empty() && g_fingerprint == fingerprint;
    ReleaseSRWLockShared(&g_lock);
    return result;
}
void reset() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_rows.clear();
    g_fingerprint = {};
    g_confirmed = false;
    ReleaseSRWLockExclusive(&g_lock);
}
/** Shared-cache restore keeps rows hidden until the installed manifest is available. */
bool restore(std::span<const Row> rows, const Fingerprint& fingerprint) noexcept {
    if (!validate(rows)) {
        return false;
    }
    try {
        Rows copy(rows.begin(), rows.end());
        AcquireSRWLockExclusive(&g_lock);
        g_rows.swap(copy);
        g_fingerprint = fingerprint;
        g_confirmed = false;
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    } catch (...) {
        return false;
    }
}
/** A mismatched package estate invalidates the restored domain before lookup. */
bool confirm(const Fingerprint& fingerprint) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const bool valid = !g_rows.empty() && g_fingerprint == fingerprint;
    if (!valid) {
        g_rows.clear();
        g_fingerprint = {};
    }
    g_confirmed = valid;
    ReleaseSRWLockExclusive(&g_lock);
    return valid;
}
bool available() noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool value = g_confirmed && !g_rows.empty();
    ReleaseSRWLockShared(&g_lock);
    return value;
}
/** The shared writer receives one checked snapshot with its source fingerprint. */
bool snapshot(std::span<Row> output, std::size_t& count, Fingerprint& fingerprint) noexcept {
    count = 0;
    fingerprint = {};
    AcquireSRWLockShared(&g_lock);
    bool valid = g_confirmed && !g_rows.empty() && output.size() >= g_rows.size();
    try {
        if (valid) {
            std::copy(g_rows.begin(), g_rows.end(), output.begin());
            count = g_rows.size();
            fingerprint = g_fingerprint;
        }
    } catch (...) {
        valid = false;
        count = 0;
        fingerprint = {};
    }
    ReleaseSRWLockShared(&g_lock);
    return valid;
}
/** Bubble identity uses the same validated package row as coordinate widths. */
bool lookup_bubble(std::string_view activity, std::uint16_t cell, std::uint8_t& bubble) noexcept {
    bubble = 255;
    AcquireSRWLockShared(&g_lock);
    const auto found = std::lower_bound(g_rows.begin(),
                                        g_rows.end(),
                                        std::pair(activity, cell),
                                        [](const Row& row, const auto& key) {
                                            const std::string_view name = row.activity.view();
                                            return name < key.first
                                                   || (name == key.first && row.cell < key.second);
                                        });
    const bool result = g_confirmed && found != g_rows.end() && found->activity.view() == activity
                        && found->cell == cell;
    if (result) {
        bubble = found->bubble;
    }
    ReleaseSRWLockShared(&g_lock);
    return result;
}
/** Unknown activity or cell mappings never acquire guessed widths. */
bool lookup(std::string_view activity,
            std::uint16_t cell,
            std::array<std::uint8_t, 3>& axisBits) noexcept {
    axisBits = {};
    AcquireSRWLockShared(&g_lock);
    const auto found = std::lower_bound(g_rows.begin(),
                                        g_rows.end(),
                                        std::pair(activity, cell),
                                        [](const Row& row, const auto& key) {
                                            const std::string_view name = row.activity.view();
                                            return name < key.first
                                                   || (name == key.first && row.cell < key.second);
                                        });
    const bool result = g_confirmed && found != g_rows.end() && found->activity.view() == activity
                        && found->cell == cell;
    if (result) {
        axisBits = found->axisBits;
    }
    ReleaseSRWLockShared(&g_lock);
    return result;
}
} // namespace sunrise::state::gameplay::entity_position_profiles
