#pragma once

#include "internal.h"

namespace sunrise::server::bap {

/** Validates and clears stale state on one already-selected ActivityClient session. */
[[nodiscard]] ActivityMissionSeedLeaseStatus mission_seed_session_status(
    Session& session, std::uint32_t scenarioRow, std::uint64_t expectedGeneration) noexcept;

/** Copies one validated session lease into the public read-only view. */
void read_mission_seed_lease(const Session& session,
                             std::size_t matchingLinks,
                             ActivityMissionSeedLeaseView& output) noexcept;

} // namespace sunrise::server::bap
