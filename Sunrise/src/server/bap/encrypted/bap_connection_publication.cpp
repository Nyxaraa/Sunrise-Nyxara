#include "bap_connection_publication.h"

#include <Windows.h>

#include <atomic>
#include <limits>

#include "../../../core/logging/log.h"
#include "../../../state/activity/runtime.h"
#include "../../gameplay/group/group_host_sessions.h"
#include "push/activity/internal.h"

namespace sunrise::server::bap::encrypted {
namespace {

/** Measured delay before the second Family-4 snapshot. */
constexpr std::uint64_t kFamily4RepushDelayMs = 400;
/** The banner pair lands the same unsolicited way and hits the same record-state race. */
constexpr std::uint64_t kBannerRepushDelayMs = 400;
/**
 * Delay before the family-two re-push owed by an emblem equip.
 *
 * Matched to the two above, which are the measured-working value for the same record-state race:
 * a snapshot answered too soon reaches the record before it writes its new state and is refused
 * silently. If a re-push ever fails to land, this constant is the one guess in the mechanism.
 */
constexpr std::uint64_t kSocialRosterRepushDelayMs = 400;
/**
 * Delay before the ability-icon re-derivation owed by a subclass selection.
 * The Client content-extraction pump that rebuilds the invalidated ability buckets runs on the
 * next few RunCallbacks pumps, well under this window.
 */
constexpr std::uint64_t kAbilityRefreshDelayMs = 500;
/**
 * How long the roster keeps its faster cadence after a load starts.
 * The slice-set load step costs 9.2 to 14.1 s, so this covers it.
 */
constexpr std::uint64_t kTransitionWindowMs = 15'000;

/** Process-lifetime generation that rejects delayed epochs after a BAP slot is reused. */
std::atomic<std::uint64_t> g_nextActivityBindingGeneration{1};

/** Releases one host-session retain when it is present. */
void release_host_generation(std::uint64_t generation) noexcept {
    if (generation != 0) {
        server::gameplay::group::release_host_session(generation);
    }
}

/** Releases every retained host row of one directory and clears it. */
void release_host_generations(AdvertisementRetains& retains) noexcept {
    for (std::size_t entry = 0; entry < retains.count; ++entry) {
        release_host_generation(retains.hostGenerations[entry]);
    }
    retains = {};
}

/** Clears all connection state rebuilt by a successful activity join. */
void reset_join_state(Session& session) noexcept {
    session.activityMemberKey = 0;
    session.activityJoinGeneration = 0;
    session.activityCharacterSoid = 0;
    session.activityKeepaliveDueTick = 0;
    session.activityRosterDueTick = 0;
    session.activityTransitionUntilTick = 0;
    session.activityClientIdentitySeenGeneration = 0;
    session.activityClientIdentityPublishedGeneration = 0;
    session.activityPatchEpoch = {};
    session.activityReplicationEpoch = {};
    session.activityRosterGroupLeases = {};
    session.activityRosterSends = 0;
    session.activityRosterRegionBubble = -1;
    session.activityHostStateRevision = 0;
    authority_query::reset(session.activityAuthorityQuery, session.activity.bindingGeneration);
    authority_reset::reset(session.activityAuthorityReset, session.activity.bindingGeneration);
    session.activityIncidentStaged = {};
    session.activityIncidentRetryDueTick = 0;
    session.activityRosterReason = 0;
    session.activityRosterStaged = {};
    session.activityRosterDecode = {};
    SecureZeroMemory(&session.activitySquadOverride, sizeof session.activitySquadOverride);
    session.activityMissionSeed = {};
    if (session.activity.role == ActivityClientRole::privateCurrent) {
        session.activity.advertisedRegion = -1;
    }
}

} // namespace

/** Reserves one process-lifetime ActivityClient generation without wrapping. */
bool reserve_activity_binding_generation(std::uint64_t& generation) noexcept {
    generation = 0;
    std::uint64_t current = g_nextActivityBindingGeneration.load();
    while (current != (std::numeric_limits<std::uint64_t>::max)()) {
        if (g_nextActivityBindingGeneration.compare_exchange_weak(current, current + 1)) {
            generation = current;
            return true;
        }
    }
    return false;
}

/** Captures the connection fields one service outcome carries. */
ConnectionFields connection_fields(const ServiceOutcome& outcome) noexcept {
    ConnectionFields fields{};
    const auto* plan = transaction_if<activity_message::ActivityPlan>(outcome);
    if (plan == nullptr) {
        return fields;
    }
    if (plan->delivery == activity_message::Delivery::joinNotifications) {
        fields.joinMemberKey = plan->entitySlotMutation.memberKey;
        fields.joinCharacterSoid = plan->joinCharacterSoid;
        fields.joinIngress = plan->joinIngress;
        fields.joinsActivity = true;
    }
    // The initial load is a transition too, and its token does not arrive for several seconds.
    fields.opensTransitionWindow =
        plan->delivery == activity_message::Delivery::joinNotifications || plan->transitionStarted;
    if (plan->mutationDomain == activity_message::MutationDomain::patchEpoch) {
        fields.patchEpoch = plan->patchEpoch;
        fields.retainsPatchEpoch = true;
    }
    fields.receivesClientIdentity =
        plan->mutationDomain == activity_message::MutationDomain::membership
        && plan->membershipMutation.kind == state::activity::membership::MutationKind::identity;
    return fields;
}

/** Records one membership body after its complete frame reaches the transport caller. */
void note_activity_membership_delivery(Session& session) noexcept {
    const std::uint64_t generation = session.activity.bindingGeneration;
    if (session.activity.role == ActivityClientRole::none || generation == 0) {
        return;
    }
    session.activityMembershipSentGeneration = generation;
    if (session.activityClientIdentitySeenGeneration == generation) {
        session.activityClientIdentityPublishedGeneration = generation;
    }
}

/** Publishes the captured connection fields after a successful commit. */
void publish_connection_fields(Session& session,
                               const transactions::Publication& publication,
                               const ConnectionFields& fields) noexcept {
    if (publication.hasActivitySessionBinding) {
        if (!publication.preservesActivitySessionBinding) {
            release_activity_connection(session);
            session.activity = publication.activity;
            reset_join_state(session);
        }
        session.activity.bindingGeneration = publication.activity.bindingGeneration;
    }
    if (fields.joinsActivity) {
        // The join burst may have staged its own membership directory; only a stale one goes.
        if (!session.activityJoinMembershipStaged) {
            discard_staged_advertisement(session);
        }
        release_host_generations(session.activityAdvertisementHeld);
        reset_join_state(session);
        session.activityMemberKey = fields.joinMemberKey;
        session.activityJoinGeneration = session.activity.bindingGeneration;
        session.activityCharacterSoid = fields.joinCharacterSoid;
    }
    if (fields.retainsPatchEpoch) {
        session.activityPatchEpoch.value = fields.patchEpoch;
        session.activityPatchEpoch.bindingGeneration = session.activity.bindingGeneration;
        session.activityPatchEpoch.seen = session.activity.role != ActivityClientRole::none;
    }
    if (fields.opensTransitionWindow) {
        session.activityTransitionUntilTick = GetTickCount64() + kTransitionWindowMs;
    }
    if (fields.receivesClientIdentity) {
        session.activityClientIdentitySeenGeneration = session.activity.bindingGeneration;
    }
    // A join resets the roster container. Its first post-region state change rebuilds the
    // participation component against the published membership.
    if (fields.joinsActivity) {
        session.activityRosterSends = 0;
        session.activityRosterGroupLeases = {};
        session.activityRosterRegionBubble = -1;
    }
    // A private join burst delivered the seed membership body; commit the matching identity so
    // State and the delivered-body record agree with what the client now holds.
    if (fields.joinsActivity && session.activity.role == ActivityClientRole::privateCurrent) {
        if (session.activityJoinMembershipStaged) {
            push::activity::adopt_join_membership_record(session);
        }
        state::activity::membership::PendingMutation seed{};
        if (!push::activity::prepare_seed_identity(session.activity.session.sessionId,
                                                   session.activityMemberKey,
                                                   session.activityCharacterSoid,
                                                   seed)
            || !state::activity::membership::commit(seed)) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=activity stage=membership result=seed_fail");
        }
    }
    // Reached only once the frame has been copied out, so this is the delivery the one-shot means.
    if (session.activityJoinMembershipStaged || session.activityAdvertisementStaged.staged) {
        note_activity_membership_delivery(session);
    }
    session.activityJoinMembershipStaged = false;
}

/** Stages one body's retained host directory until the membership frame has an outcome. */
void stage_activity_advertisement(Session& session, const AdvertisementRetains& retains) noexcept {
    discard_staged_advertisement(session);
    session.activityAdvertisementStaged.retains = retains;
    session.activityAdvertisementStaged.staged = true;
}

/** Publishes one staged directory's retains. */
void commit_staged_advertisement(Session& session) noexcept {
    if (!session.activityAdvertisementStaged.staged) {
        return;
    }
    release_host_generations(session.activityAdvertisementHeld);
    session.activityAdvertisementHeld = session.activityAdvertisementStaged.retains;
    session.activityAdvertisementStaged = {};
}

/** Releases one staged directory's retains. */
void discard_staged_advertisement(Session& session) noexcept {
    if (session.activityAdvertisementStaged.staged) {
        release_host_generations(session.activityAdvertisementStaged.retains);
        session.activityAdvertisementStaged = {};
    }
}

/** Releases every exact activity owner held by one BAP connection. */
void release_activity_connection(Session& session) noexcept {
    discard_staged_advertisement(session);
    release_host_generations(session.activityAdvertisementHeld);
    if (session.activity.hostGeneration != 0) {
        server::gameplay::group::release_host_session(session.activity.hostGeneration);
    }
    if (session.activity.session.sessionId != state::activity::kAbsentSessionId) {
        state::activity::release_binding(session.activity.session);
    }
    session.activity = {};
    authority_query::reset(session.activityAuthorityQuery, 0);
    authority_reset::reset(session.activityAuthorityReset, 0);
    session.activityPatchEpoch = {};
    session.activityReplicationEpoch = {};
    session.activityJoinGeneration = 0;
    session.activityClientIdentitySeenGeneration = 0;
    session.activityClientIdentityPublishedGeneration = 0;
    session.activityRosterStaged.decodeMap = {};
    session.activityRosterDecode = {};
}

/** Arms the owed Family-4 and banner re-pushes when the queuez publication asks for them. */
void arm_repushes(Session& session, const queuez::StagedPublication& queuezPublication) noexcept {
    const std::uint64_t now = GetTickCount64();
    if (queuezPublication.armsAbilityRefresh) {
        session.abilityRefreshDueTick = now + kAbilityRefreshDelayMs;
        session.abilityRefreshArmed = true;
    }
    if (queuezPublication.armsFamily4Repush && queuezPublication.family4RepushRoot != 0) {
        session.family4RepushDueTick = now + kFamily4RepushDelayMs;
        session.family4RepushRoot = queuezPublication.family4RepushRoot;
        session.family4RepushArmed = true;
    }
    // Armed on its own signal, not on family four's. Family zero re-subscribes on every record
    // cycle, and each subscribe needs a delayed copy because the immediate answer arrives too soon.
    if (queuezPublication.armsBannerRepush && queuezPublication.bannerRepushRoot != 0) {
        session.bannerRepushDueTick = now + kBannerRepushDelayMs;
        session.bannerRepushRoot = queuezPublication.bannerRepushRoot;
        session.bannerRepushArmed = true;
    }
    // Recorded whenever a family-two subscribe was answered, so a later equip has a root to
    // publish against. A peer that never subscribed to family two keeps root zero and is left
    // alone below, which is the correct no-op for it.
    if (queuezPublication.socialRosterRepushRoot != 0) {
        session.socialRosterRepushRoot = queuezPublication.socialRosterRepushRoot;
    }
    // Its own arm on its own signal, for the reason recorded above: the banner arm was once
    // driven from the wrong family's and took the connection down. Re-arming is idempotent and
    // coalescing, so a burst of equips owes one delayed send rather than one each.
    if (queuezPublication.rearmsSocialRosterRepush && session.socialRosterRepushRoot != 0) {
        session.socialRosterRepushDueTick = now + kSocialRosterRepushDelayMs;
        session.socialRosterRepushArmed = true;
    }
}

} // namespace sunrise::server::bap::encrypted
