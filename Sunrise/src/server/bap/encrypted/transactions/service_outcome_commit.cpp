#include "service_outcome_commit.h"

#include <array>
#include <cstdio>

#include "../../../../client/content/investment/worker.h"
#include "../../../../core/logging/log.h"
#include "../../../../middleware/bap/activity_message/activity_join_result_encoder.h"
#include "../../../../state/activity/runtime.h"
#include "../../../../state/matchmaking/matchmaking_state.h"
#include "../../../../state/runtime/runtime.h"
#include "../../../../state/vendors/answered_interactions.h"
#include "../bap_connection_publication.h"
#include "../internal.h"

namespace sunrise::server::bap::encrypted::transactions {

namespace slots = state::activity::entity_slots;

/** Log names for each lease operation, in the enum's own order. */
static constexpr std::array<const char*, 4> kLeaseKinds = {"none", "join", "grant", "release"};

/** Retains one newly committed private ActivityClient generation for its BAP link. */
[[nodiscard]] static bool retain_private(std::uint64_t sessionId,
                                         Publication& publication) noexcept {
    state::activity::SessionBinding binding{};
    if (!state::activity::snapshot_binding(sessionId, binding)
        || !state::activity::retain_binding(binding)) {
        return false;
    }
    publication.activity.session = binding;
    publication.activity.source = binding;
    publication.activity.role = ActivityClientRole::privateCurrent;
    publication.activity.replicationEpoch =
        middleware::bap::activity_message::join_result::kInitialReplicationEpoch;
    publication.hasActivitySessionBinding = true;
    return true;
}

/** Retains one exact advertised public target before its join mutation commits. */
[[nodiscard]] static bool retain_public(const activity_message::ActivityPlan& plan,
                                        Publication& publication) noexcept {
    server::gameplay::group::HostSessionBinding current{};
    if (!server::gameplay::group::host_session_for_activity(plan.sessionId, current)
        || current.generation != plan.publicHost.generation
        || current.groupSessionId != plan.publicHost.groupSessionId
        || current.regionIndex != plan.publicHost.regionIndex
        || current.source.sessionId != plan.publicHost.source.sessionId
        || current.source.createdRevision != plan.publicHost.source.createdRevision
        || current.target.sessionId != plan.publicHost.target.sessionId
        || current.target.createdRevision != plan.publicHost.target.createdRevision
        || !state::activity::binding_matches(current.source)
        || !state::activity::binding_matches(current.target)
        || !server::gameplay::group::retain_host_session(current.generation)) {
        return false;
    }
    if (!state::activity::retain_binding(current.target)) {
        server::gameplay::group::release_host_session(current.generation);
        return false;
    }
    publication.activity.session = current.target;
    publication.activity.source = current.source;
    publication.activity.groupSessionId = current.groupSessionId;
    publication.activity.hostGeneration = current.generation;
    publication.activity.advertisedRegion = current.regionIndex;
    publication.activity.role = ActivityClientRole::publicTarget;
    publication.activity.replicationEpoch =
        middleware::bap::activity_message::join_result::kInitialReplicationEpoch;
    publication.hasActivitySessionBinding = true;
    return true;
}

/** Releases provisional activity owners when the following State commit fails. */
static void discard_activity_publication(Publication& publication) noexcept {
    if (publication.activity.hostGeneration != 0) {
        server::gameplay::group::release_host_session(publication.activity.hostGeneration);
    }
    if (publication.activity.session.sessionId != state::activity::kAbsentSessionId) {
        state::activity::release_binding(publication.activity.session);
    }
    publication = {};
}

/**
 * Reports one entity-slot lease change.
 * The client prints only `failed to create` when it has no free index, and nothing else on this
 * path reports the lease. Without this line a failed create reads the same as an empty grant.
 * @param mutation Plan as it was before the commit consumed it.
 * @param committed Whether the commit succeeded.
 */
static void report_lease(const slots::PendingMutation& mutation, bool committed) noexcept {
    if (committed) {
        return;
    }
    std::size_t held = 0;
    std::size_t reserved = 0;
    const bool known = slots::lease_counts(mutation.sessionId, held, reserved);
    const auto kind = static_cast<std::size_t>(mutation.kind);
    std::array<char, core::log::kLineCapacity> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=activity stage=entity_slots result=fail kind=%s soid=0x%llX "
                      "requested=%zu picked=%zu held=%zu reserved=%zu returned=%zu known=%u",
                      kind < kLeaseKinds.size() ? kLeaseKinds[kind] : "bad",
                      static_cast<unsigned long long>(mutation.sessionId),
                      mutation.requestedCount,
                      slots::slot_count(mutation.mask),
                      held,
                      reserved,
                      // Only a release carries one, so every other kind reports zero. A release
                      // whose returned set and picked set disagree means the two ledgers have
                      // diverged, which nothing else on this path would show.
                      slots::slot_count(mutation.returnedMask),
                      known ? 1U : 0U);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] static bool report_commit(bool committed, const char* failure) noexcept {
    if (!committed) {
        core::log::write(core::log::Channel::server, core::log::Level::warn, failure);
    }
    return committed;
}

/**
 * Commits at most one delayed State transaction.
 * @param outcome Checked service result whose pending transaction is used up.
 * @param publication Gets connection fields to publish after the output copy.
 * @return True when there is no transaction, or the one transaction commits.
 */
bool commit(ServiceOutcome& outcome, Publication& publication, const char*& reason) noexcept {
    publication = {};
    reason = "none";
    if (auto* allocation = transaction_if<state::activity::PendingAllocation>(outcome)) {
        const std::uint64_t sessionId = allocation->sessionId;
        std::uint64_t bindingGeneration = 0;
        if (sessionId == state::activity::kAbsentSessionId
            || !reserve_activity_binding_generation(bindingGeneration)
            || !state::activity::commit(*allocation)) {
            reason = "allocation";
            return false;
        }
        if (!retain_private(sessionId, publication)) {
            static_cast<void>(state::activity::release_session(sessionId));
            reason = "retain_private";
            return false;
        }
        publication.activity.bindingGeneration = bindingGeneration;
        return true;
    }
    if (auto* plan = transaction_if<activity_message::ActivityPlan>(outcome)) {
        if (plan->mutationDomain == activity_message::MutationDomain::entitySlots) {
            const bool joins = plan->delivery == activity_message::Delivery::joinNotifications;
            const bool validJoinIntent =
                plan->bindingIntent == activity_message::BindingIntent::preserveCurrent
                || plan->bindingIntent == activity_message::BindingIntent::publicTarget;
            if (joins && !validJoinIntent) {
                reason = "join_intent";
                return false;
            }
            std::uint64_t bindingGeneration = 0;
            if (joins && !reserve_activity_binding_generation(bindingGeneration)) {
                reason = "join_generation";
                return false;
            }
            if (joins && plan->bindingIntent == activity_message::BindingIntent::publicTarget
                && !retain_public(*plan, publication)) {
                reason = "retain_public";
                return false;
            }
            // The commit consumes the plan, so the counts are taken from a copy of it.
            const slots::PendingMutation attempted = plan->entitySlotMutation;
            const bool committed = slots::commit(plan->entitySlotMutation);
            report_lease(attempted, committed);
            if (!committed) {
                discard_activity_publication(publication);
                reason = "entity_slots";
                return false;
            }
            // The keepalive only finds a link that is bound to a session. A link that allocated
            // its own session carries the same id, so this rebinds it to itself.
            if (joins && plan->sessionId != state::activity::kAbsentSessionId) {
                publication.hasActivitySessionBinding = true;
                if (plan->bindingIntent == activity_message::BindingIntent::preserveCurrent) {
                    publication.preservesActivitySessionBinding = true;
                } else if (plan->bindingIntent != activity_message::BindingIntent::publicTarget) {
                    discard_activity_publication(publication);
                    reason = "bind_intent";
                    return false;
                }
                publication.activity.bindingGeneration = bindingGeneration;
            }
            return true;
        }
        if (plan->mutationDomain == activity_message::MutationDomain::membership) {
            reason = "membership";
            return state::activity::membership::commit(plan->membershipMutation,
                                                       &publication.clientState);
        }
        if (plan->mutationDomain == activity_message::MutationDomain::authorityQuery) {
            reason = "authority_query";
            return plan->authorityQuery.pending;
        }
        if (plan->mutationDomain == activity_message::MutationDomain::authorityReset) {
            reason = "authority_reset";
            return plan->authorityReset.pending;
        }
        // The retained patch epoch is connection state, so it commits nothing here.
        reason = "mutation_domain";
        return plan->mutationDomain == activity_message::MutationDomain::patchEpoch;
    }
    if (auto* mutation = transaction_if<state::matchmaking::PendingMutation>(outcome)) {
        reason = "matchmaking";
        return state::matchmaking::commit(*mutation);
    }
    if (auto* mutation = transaction_if<state::PendingSettingsUpdate>(outcome)) {
        const bool committed = state::commit_settings_update(*mutation);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=ws701 stage=transaction_commit result=ok"
                                   : "ev=ws701 stage=transaction_commit result=fail");
        return committed;
    }
    if (auto* transaction = transaction_if<EquipmentSwapTransaction>(outcome)) {
        if (transaction->pending == nullptr) {
            return false;
        }
        auto& pending = *transaction->pending;
        const bool isSubclassSlot =
            pending.equipmentSlotIndex
            == static_cast<std::size_t>(state::account::inventory::EquipmentSlot::subclass);
        const bool committed = state::commit_equipment_swap(*transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=equip stage=transaction_commit result=ok"
                                   : "ev=equip stage=transaction_commit result=fail");
        reason = "equip";
        if (committed && isSubclassSlot) {
            // The equipped subclass just changed, which makes the published ability buckets
            // stale the same way an ability-entry pick does. Wake the investment worker so the
            // character screen stops showing the previous subclass's resolution.
            client::content::investment::worker::request_slice();
        }
        return true;
    }
    if (auto* transaction = transaction_if<SubclassSelectionTransaction>(outcome)) {
        if (transaction->pending == nullptr) {
            return false;
        }
        reason = "subclass_select";
        if (!report_commit(state::commit_subclass_selection(*transaction->pending),
                           "ev=subclass_select stage=transaction_commit result=fail")) {
            return false;
        }
        // Rebuild the ability buckets keyed by the changed selection.
        client::content::investment::worker::request_slice();
        return true;
    }
    if (auto* transaction = transaction_if<ItemAcquisitionTransaction>(outcome)) {
        const bool committed = transaction->pending != nullptr
                               && state::commit_item_acquisition(*transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=acquire stage=transaction_commit result=ok"
                                   : "ev=acquire stage=transaction_commit result=fail");
        reason = "acquire";
        // The item is in the inventory, so the interaction that offered it is answered. This is
        // the point the shipped game appends its own entry, and why the answer waited.
        if (committed && transaction->answeredVendor != state::vendors::kAbsentIndex) {
            (void)state::vendors::answer_shown(transaction->answeredVendor);
        }
        return committed;
    }
    if (auto* transaction = transaction_if<SocketPlugTransaction>(outcome)) {
        const bool committed = transaction->pending != nullptr
                               && state::commit_socket_plug(*transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=socket_plug stage=transaction_commit result=ok"
                                   : "ev=socket_plug stage=transaction_commit result=fail");
        reason = "socket_plug";
        return committed;
    }
    if (auto* transaction = transaction_if<ItemStateTransaction>(outcome)) {
        const bool committed = transaction->pending != nullptr
                               && state::commit_item_state(*transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=item_state stage=transaction_commit result=ok"
                                   : "ev=item_state stage=transaction_commit result=fail");
        reason = "item_state";
        return committed;
    }
    if (auto* transaction = transaction_if<CurrentActivityTransaction>(outcome)) {
        const bool committed = state::commit_current_activity(transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=current_activity stage=transaction_commit result=ok"
                                   : "ev=current_activity stage=transaction_commit result=fail");
        reason = "current_activity";
        return committed;
    }
    if (auto* transaction = transaction_if<ProfileItemAcquisitionTransaction>(outcome)) {
        const bool committed = transaction->pending != nullptr
                               && state::commit_profile_item_acquisition(*transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=profile_acquire stage=transaction_commit result=ok"
                                   : "ev=profile_acquire stage=transaction_commit result=fail");
        reason = "profile_acquire";
        if (committed && transaction->answeredVendor != state::vendors::kAbsentIndex) {
            (void)state::vendors::answer_shown(transaction->answeredVendor);
        }
        return committed;
    }
    if (auto* transaction = transaction_if<ItemDismantleTransaction>(outcome)) {
        const bool committed = transaction->pending != nullptr
                               && state::commit_item_dismantle(*transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=dismantle stage=transaction_commit result=ok"
                                   : "ev=dismantle stage=transaction_commit result=fail");
        reason = "dismantle";
        return committed;
    }
    if (auto* transaction = transaction_if<ArtifactPurchaseTransaction>(outcome)) {
        reason = "artifact_purchase";
        return transaction->pending != nullptr
               && report_commit(state::commit_artifact_mod_unlock(*transaction->pending),
                                "ev=ws901 stage=transaction_commit result=fail");
    }
    if (auto* transaction = transaction_if<SeasonPassRewardTransaction>(outcome)) {
        reason = "season_pass_reward";
        return transaction->pending != nullptr
               && report_commit(state::commit_season_pass_reward(*transaction->pending),
                                "ev=ws2400 stage=transaction_commit result=fail");
    }
    if (auto* transaction = transaction_if<RecordRewardGrantTransaction>(outcome)) {
        reason = "record_reward";
        return transaction->pending != nullptr
               && report_commit(state::commit_record_reward(*transaction->pending),
                                "ev=record_reward stage=transaction_commit result=fail");
    }
    return true;
}

} // namespace sunrise::server::bap::encrypted::transactions
