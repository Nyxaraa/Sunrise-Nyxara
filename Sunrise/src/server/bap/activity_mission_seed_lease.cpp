#include "activity_mission_seed_lease.h"
#include "../../state/activity/membership/activity_membership_query.h"

namespace sunrise::server::bap {

void update_mission_seed_retirement(Session& session) noexcept {
    auto& lease = session.activityMissionSeed;
    if (!lease.retirementRequested || !lease.retirementPublished
        || lease.retirementAcknowledged || !lease.regionArrivalPending) return;
    const auto region = static_cast<std::int32_t>(lease.previousPlan.effectiveRegion);
    const auto placement = state::activity::membership::reported_placement(
        session.activity.session.sessionId);
    if (placement.currentRegion != region
        || state::activity::membership::instantiated_region(placement) != region) return;
    if (lease.retiredGroupCount == 0
        || roster_retirement_received(session.activityRosterMirror,
                                      std::span(lease.retiredGroups).first(lease.retiredGroupCount),
                                      session.activity.bindingGeneration,
                                      lease.retirementReceiptFloor,
                                      region)) {
        lease.retirementAcknowledged = true;
    }
}

/** Validates and clears stale state on one already-selected ActivityClient session. */
ActivityMissionSeedLeaseStatus mission_seed_session_status(
    Session& session, std::uint32_t scenarioRow, std::uint64_t expectedGeneration) noexcept {
    if (expectedGeneration == 0 || session.activity.bindingGeneration != expectedGeneration) {
        return ActivityMissionSeedLeaseStatus::staleActivityClient;
    }
    MissionSeedLease& lease = session.activityMissionSeed;
    if (lease.configured && lease.bindingGeneration != session.activity.bindingGeneration) {
        lease = {};
    }
    if (lease.configured && lease.plan.scenarioRow != scenarioRow) {
        return ActivityMissionSeedLeaseStatus::wrongScenario;
    }
    return ActivityMissionSeedLeaseStatus::ready;
}

/** Copies one validated session lease into the public read-only view. */
void read_mission_seed_lease(const Session& session,
                             std::size_t matchingLinks,
                             ActivityMissionSeedLeaseView& output) noexcept {
    const MissionSeedLease& lease = session.activityMissionSeed;
    output = {};
    output.plan = lease.configured ? lease.plan : ActivityMissionSeedPlan{};
    output.matchingLinks = matchingLinks;
    output.activityClientGeneration = session.activity.bindingGeneration;
    output.revision = lease.revision;
    output.publishedRevision = lease.publishedRevision;
    output.configured = lease.configured;
    output.publicationPending = lease.configured && lease.revision != lease.publishedRevision;
    output.regionArrivalPending = lease.configured && lease.regionArrivalPending;
    output.retirementPending = lease.configured && lease.retirementRequested
                               && !lease.retirementAcknowledged;
}

} // namespace sunrise::server::bap
