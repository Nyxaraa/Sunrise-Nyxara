#pragma once

#include <cstddef>
#include <cstdint>

#include "internal.h"
#include "mission_seed_world_change.h"

namespace sunrise::server::bap::encrypted::push::activity {

/** Outcome of adding one enabled SDK selected-state roster lease to a roster snapshot. */
enum class MissionSeedRosterResult : std::uint8_t {
    inactive,
    ready,
    refused,
};

/**
 * Checks the exact authored region owed by an unpublished selected-state revision.
 * The selected plan is the publication source while a client moves between states.
 * So a pending revision takes the plan's region, not the slice-set base and not an old report.
 */
[[nodiscard]] constexpr bool
mission_seed_publication_region_ready(bool enabled,
                                      std::uint64_t revision,
                                      std::uint64_t publishedRevision,
                                      std::uint32_t pinnedEffectiveRegion,
                                      std::uint32_t selectedEffectiveRegion) noexcept {
    const bool pendingEnable = enabled && revision != publishedRevision;
    return !pendingEnable || selectedEffectiveRegion == pinnedEffectiveRegion;
}

/**
 * Adds the exact selected-state Auth groups before retained squad groups are considered.
 * The full set goes out only once the client reports holding the selected region.
 * @param hostedBubbles One bit per bubble this link hosts; a region outside them adds nothing.
 * @param refresh The client refresh this body answers, or null.
 */
[[nodiscard]] MissionSeedRosterResult
append_initial_mission_seed(Session& session,
                            Scratch& scratch,
                            message::Snapshot& snapshot,
                            std::uint32_t effectiveRegion,
                            std::uint64_t hostedBubbles,
                            std::size_t canonicalGroupCount,
                            const RefreshReport* refresh) noexcept;

} // namespace sunrise::server::bap::encrypted::push::activity
