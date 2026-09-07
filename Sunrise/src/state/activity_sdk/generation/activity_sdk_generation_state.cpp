#include <Windows.h>

#include <algorithm>
#include <cstring>

#include "internal.h"
#include "runtime.h"

namespace sunrise::state::activity_sdk::generation {
namespace {

SRWLOCK g_lock{SRWLOCK_INIT};
Snapshot g_snapshot{};

} // namespace

/** Returns one copy without exposing State-owned storage. */
Snapshot snapshot() noexcept {
    AcquireSRWLockShared(&g_lock);
    const Snapshot value = g_snapshot;
    ReleaseSRWLockShared(&g_lock);
    return value;
}

/** Maps every job state to stable display text. */
const char* status_name(Status value) noexcept {
    switch (value) {
    case Status::disabled:
        return "disabled";
    case Status::waiting:
        return "waiting";
    case Status::preparing:
        return "preparing";
    case Status::building:
        return "building";
    case Status::publishing:
        return "publishing";
    case Status::ready:
        return "ready";
    case Status::failed:
        return "failed";
    case Status::cancelled:
        return "cancelled";
    }
    return "unknown";
}

namespace internal {

/** Replaces the complete fixed snapshot under one lock. */
void publish(Status status,
             std::uint32_t current,
             std::uint32_t total,
             std::uint32_t scenarioTag,
             std::string_view detail) noexcept {
    Snapshot next{};
    next.status = status;
    next.current = current;
    next.total = total;
    next.scenarioTag = scenarioTag;
    const std::size_t length = (std::min)(detail.size(), next.detail.size() - 1);
    if (length != 0) {
        std::memcpy(next.detail.data(), detail.data(), length);
    }

    AcquireSRWLockExclusive(&g_lock);
    next.revision = g_snapshot.revision + 1;
    g_snapshot = next;
    ReleaseSRWLockExclusive(&g_lock);
}

/** Clears all job state while keeping revisions monotonic. */
void clear() noexcept {
    publish(Status::disabled, 0, 0, 0, "generation is disabled");
}

} // namespace internal
} // namespace sunrise::state::activity_sdk::generation
