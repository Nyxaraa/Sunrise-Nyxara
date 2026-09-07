#include "entity_object_types.h"

#include <Windows.h>

#include <algorithm>
namespace sunrise::state::gameplay::entity_object_types {
namespace {
SRWLOCK g_lock{SRWLOCK_INIT};
Rows g_rows;
Fingerprint g_fingerprint{};
bool g_confirmed{};
} // namespace
/** Only sorted, unique, reciprocal package records enter the role catalogue. */
bool validate(std::span<const Row> rows) noexcept {
    if (rows.empty() || rows.size() > kMaximumRows) {
        return false;
    }
    std::uint32_t previous = 0;
    for (const auto& row : rows) {
        if (!row.rsatTag || row.rsatTag == 0xFFFFFFFFU || !row.definitionTag
            || row.definitionTag == 0xFFFFFFFFU || row.objectType > kMaximumObjectType
            || row.rsatTag <= previous) {
            return false;
        }
        previous = row.rsatTag;
    }
    return true;
}
/** Package extraction publishes a complete immutable catalogue. */
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
/** Shared-cache rows remain hidden until the installed manifest is confirmed. */
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
/** A mismatched package estate cannot supply an object classification. */
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
    const bool valid = g_confirmed && !g_rows.empty();
    ReleaseSRWLockShared(&g_lock);
    return valid;
}
/** The shared writer receives the same fingerprint as the extracted rows. */
bool snapshot(std::span<Row> output, std::size_t& count, Fingerprint& fingerprint) noexcept {
    count = 0;
    fingerprint = {};
    AcquireSRWLockShared(&g_lock);
    const bool valid = g_confirmed && !g_rows.empty() && output.size() >= g_rows.size();
    if (valid) {
        std::copy(g_rows.begin(), g_rows.end(), output.begin());
        count = g_rows.size();
        fingerprint = g_fingerprint;
    }
    ReleaseSRWLockShared(&g_lock);
    return valid;
}
/** Packet classification reads only a confirmed package-derived row. */
bool lookup(std::uint32_t rsat, Row& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    const auto found =
        std::lower_bound(g_rows.begin(), g_rows.end(), rsat, [](const Row& row, auto tag) {
            return row.rsatTag < tag;
        });
    const bool valid = g_confirmed && found != g_rows.end() && found->rsatTag == rsat;
    if (valid) {
        output = *found;
    }
    ReleaseSRWLockShared(&g_lock);
    return valid;
}
/** Late package facts never rewrite canonical network identities or source relations. */
bool enrich_snapshot(std::span<entity_identity::Identity> rows) noexcept {
    AcquireSRWLockShared(&g_lock);
    bool valid = true;
    if (g_confirmed) {
        for (auto& row : rows) {
            if (!row.known || !row.present || row.type != 0 || !row.metadata.hasRsat) {
                continue;
            }
            const auto found = std::lower_bound(
                g_rows.begin(),
                g_rows.end(),
                row.metadata.rsatTag,
                [](const Row& candidate, auto tag) { return candidate.rsatTag < tag; });
            if (found == g_rows.end() || found->rsatTag != row.metadata.rsatTag) {
                continue;
            }
            if (row.metadata.hasObjectType && row.metadata.objectType != found->objectType) {
                valid = false;
                break;
            }
            row.metadata.objectType = found->objectType;
            row.metadata.hasObjectType = true;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return valid;
}
void reset() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_rows.clear();
    g_fingerprint = {};
    g_confirmed = false;
    ReleaseSRWLockExclusive(&g_lock);
}
} // namespace sunrise::state::gameplay::entity_object_types
