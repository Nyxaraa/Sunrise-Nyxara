#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../core/logging/log.h"
#include "../../../middleware/datagen/definitions.h"
#include "../../../middleware/encoding/byte_order.h"
#include "../../../middleware/secure_channel/runtime.h"
#include "../../../state/runtime/runtime.h"
#include "../../../client/hooks/network/investment/investment_derived_rebuild.h"
#include "../../activity/host_runtime.h"
#include "../activity_authority_query_owner.h"
#include "../activity_authority_reset_owner.h"
#include "../internal.h"
#include "activity_transaction/activity_transaction_notifications.h"
#include "bap_connection_publication.h"
#include "internal.h"
#include "push/activity/activity_roster_push.h"
#include "queuez/queuez_outcome_staging.h"
#include "transactions/service_outcome_commit.h"

namespace sunrise::server::bap::encrypted {
namespace {

/** Records decoded service traffic while reconstructing the investment-signin contract. */
void report_service_traffic(const middleware::bap::RequestFrame& frame,
                            const ServiceRoute& route,
                            std::size_t responseBodySize) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=bap stage=service svc=%u task=%u request_bytes=%zu "
                                      "response_svc=%u response_bytes=%zu",
                                      static_cast<unsigned>(frame.serviceId),
                                      static_cast<unsigned>(frame.taskId),
                                      frame.body.size(),
                                      static_cast<unsigned>(route.response),
                                      responseBodySize);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Wipes the part of one scratch buffer that may hold written bytes.
 * @param buffer Lock-owned scratch storage.
 * @param size Largest prefix that may hold transformed bytes.
 */
void clear_prefix(std::span<std::byte> buffer, std::size_t size) noexcept {
    SecureZeroMemory(buffer.data(), (std::min)(buffer.size(), size));
}

/** Rejects stale peer mutations until every current instance object is resident. */
[[nodiscard]] bool
manifest_covers_account_instances(const queuez::SessionState& queuezState) noexcept {
    if (!queuez::valid(queuezState) || !queuezState.family4Active) {
        return false;
    }
    const state::AccountState account = state::account_snapshot();
    if (!state::account::valid(account) || account.primarySoid != queuezState.family4RootSoid) {
        return false;
    }
    const auto resident = [&](std::uint64_t soid, std::uint32_t definitionId) noexcept {
        return std::count_if(
                   queuezState.family4Residents.cbegin(),
                   queuezState.family4Residents.cbegin() + queuezState.family4ResidentCount,
                   [&](const queuez::ResidentObject& object) noexcept {
                       return object.objectSoid == soid && object.definitionId == definitionId;
                   })
               == 1;
    };
    if (!resident(account.primarySoid, middleware::datagen::kAccountObjectId)) {
        return false;
    }
    const std::uint64_t selectedCharacter = state::account::selected_character_soid(account);
    if (selectedCharacter != 0
        && !resident(selectedCharacter, middleware::datagen::kCharacterObjectId)) {
        return false;
    }
    for (std::size_t characterIndex = 0; characterIndex < account.characterCount;
         ++characterIndex) {
        const state::CharacterState& character = account.characters[characterIndex];
        for (const auto& equipped : character.equipment.slots) {
            if (equipped.has_value()
                && !resident(equipped->instanceSoid, middleware::datagen::kItemInstanceObjectId)) {
                return false;
            }
        }
        for (std::size_t itemIndex = 0; itemIndex < character.inventory.count; ++itemIndex) {
            if (!resident(character.inventory.values[itemIndex].instanceSoid,
                          middleware::datagen::kItemInstanceObjectId)) {
                return false;
            }
        }
    }
    for (std::size_t itemIndex = 0; itemIndex < account.profileItemCount; ++itemIndex) {
        const std::uint64_t instanceSoid = account.profileItems[itemIndex].instanceSoid;
        if (instanceSoid != 0
            && !resident(instanceSoid, middleware::datagen::kItemInstanceObjectId)) {
            return false;
        }
    }
    return true;
}

/** Appends one join only after its exact ActivityClient binding has committed and published. */
void record_committed_join(Session& session, const ConnectionFields& fields) noexcept {
    if (!fields.joinsActivity || !fields.joinIngress.prepared) {
        return;
    }
    server::activity::host::ClientMessageInput input{};
    input.binding = session.activity.session;
    input.sourceGeneration = session.activity.bindingGeneration;
    input.payloadFingerprint = fields.joinIngress.payloadFingerprint;
    input.messageType = 3;
    input.payloadBytes = fields.joinIngress.payloadBytes;
    input.peerHeardMask = fields.joinIngress.peerHeardMask;
    input.consumedBits = fields.joinIngress.consumedBits;
    input.hasPayloadFingerprint = fields.joinIngress.hasPayloadFingerprint;
    const std::uint64_t payloadBits =
        static_cast<std::uint64_t>(input.payloadBytes) * middleware::encoding::kBitsPerByte;
    input.status = payloadBits > input.consumedBits
                       ? server::activity::host::ClientMessageStatus::prefixOnly
                       : server::activity::host::ClientMessageStatus::decoded;
    static_cast<void>(server::activity::host::record_client_message(input));
}

/** Queues one safe msg-22 after-image only after State and connection publication commit. */
void submit_committed_client_state(const activity_message::ActivityPlan& plan,
                                   const transactions::Publication& publication) noexcept {
    // A committed msg 22 that changed nothing material is the client's settle report, sent once
    // spawn-in completes. It carries no region, spawn or teleport delta, so the surface reads all
    // three as absent, but it still arrives: the script needs it to time the opening line.
    if (!plan.clientState.pending || !publication.clientState.committed) {
        return;
    }
    server::activity::host::ClientStateChangeInput input{};
    input.binding = plan.clientState.binding;
    input.state = publication.clientState;
    input.sourceGeneration = plan.clientState.sourceGeneration;
    input.clientMessageSequence = plan.clientState.clientMessageSequence;
    input.payloadBytes = plan.clientState.payloadBytes;
    if (!server::activity::host::submit_client_state_change(input)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=activity stage=client_state_ingress result=refused");
    }
}

/** Queues exact entity-slot demand only after its grant transaction commits. */
void submit_committed_entity_slots_requested(const activity_message::ActivityPlan& plan) noexcept {
    if (!plan.entitySlotsRequested.pending) {
        return;
    }
    server::activity::host::EntitySlotsRequestedInput input{};
    input.binding = plan.entitySlotsRequested.binding;
    input.sourceGeneration = plan.entitySlotsRequested.sourceGeneration;
    input.clientMessageSequence = plan.entitySlotsRequested.clientMessageSequence;
    input.requestedCount = plan.entitySlotsRequested.requestedCount;
    if (!server::activity::host::submit_entity_slots_requested(input)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=activity stage=entity_slots_requested result=refused");
    }
}

/** Applies one query answer only after its authenticated service frame commits. */
void submit_committed_authority_answer(Session& session,
                                       const activity_message::ActivityPlan& plan) noexcept {
    if (!plan.authorityQuery.pending
        || plan.mutationDomain != activity_message::MutationDomain::authorityQuery) {
        return;
    }
    const authority_query::AnswerStatus status =
        authority_query::submit(session.activityAuthorityQuery,
                                plan.authorityQuery.sourceGeneration,
                                GetTickCount64(),
                                plan.authorityQuery.answer);
    if (status != authority_query::AnswerStatus::bubbleAccepted
        && status != authority_query::AnswerStatus::complete) {
        std::array<char, core::log::kLineCapacity> line{};
        const int count =
            std::snprintf(line.data(),
                          line.size(),
                          "ev=activity stage=authority_query result=refused status=%u",
                          static_cast<unsigned>(status));
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
    }
}

/** Applies one reset acknowledgement only after its authenticated service frame commits. */
void submit_committed_authority_reset(Session& session,
                                      const activity_message::ActivityPlan& plan) noexcept {
    if (!plan.authorityReset.pending
        || plan.mutationDomain != activity_message::MutationDomain::authorityReset) {
        return;
    }
    const authority_reset::AcknowledgementStatus status =
        authority_reset::submit(session.activityAuthorityReset,
                                plan.authorityReset.sourceGeneration,
                                GetTickCount64(),
                                plan.authorityReset.answer);
    if (status != authority_reset::AcknowledgementStatus::complete) {
        std::array<char, core::log::kLineCapacity> line{};
        const int count =
            std::snprintf(line.data(),
                          line.size(),
                          "ev=activity stage=authority_reset result=refused status=%u",
                          static_cast<unsigned>(status));
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
    }
}

} // namespace

/**
 * Authenticates and answers one supported encrypted post-bootstrap request.
 * @param session Connection-owned authentication and nonce state.
 * @param scratch Lock-owned transform buffers kept off the Client thread stack.
 * @param outer Validated encrypted outer frame.
 * @param response Caller-owned complete-frame storage.
 * @param written Receives encoded response bytes.
 * @return True when routing succeeds and any response fits, commits State, and publishes its nonce.
 */
bool consume(Session& session,
             Scratch& scratch,
             const middleware::bap::OuterFrame& outer,
             std::span<std::byte> response,
             std::size_t& written) noexcept {
    written = 0;
    session.accountMutationPublished = false;
    if (!session.authenticated) {
        // Staying silent here looks the same as a decode fault, and both look like a dead link.
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap stage=encrypted result=drop reason=unauthenticated");
        return false;
    }

    std::size_t plaintextSize = 0;
    if (!middleware::secure_channel::open_frame(session.sessionKey,
                                                session.receiveNonce,
                                                outer.payload,
                                                scratch.plaintext,
                                                plaintextSize)) {
        const std::size_t possiblePlaintextSize =
            outer.payload.size() >= middleware::secure_channel::kFrameTagSize
                ? outer.payload.size() - middleware::secure_channel::kFrameTagSize
                : 0;
        clear_prefix(scratch.plaintext, possiblePlaintextSize);
        // The service is unreadable while the frame is sealed, so this line names no service.
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=none stage=decrypt result=fail");
        return false;
    }
    // Authentication consumes the receive nonce even when the inner service is unsupported.
    middleware::secure_channel::advance_nonce(session.receiveNonce);

    middleware::bap::RequestFrame frame;
    ServiceRoute route;
    std::size_t responseBodySize = 0;
    std::size_t framedSize = 0;
    ServiceOutcome outcome{};
    transactions::Publication publication{};
    queuez::SessionState nextQueuez = session.queuez;
    bool publishesQueuez = false;
    bool handled =
        middleware::bap::parse_request_payload(std::span(scratch.plaintext).first(plaintextSize),
                                               middleware::bap::FrameType::encrypted,
                                               frame)
        && routing::resolve(frame.serviceId, route);
    if (!handled) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=none stage=parse result=fail");
    }
    const bool processesBody = handled && route.responseMode != ResponseMode::none;
    const bool sendsReply = handled && route.responseMode == ResponseMode::reply;
    bool staleWebAction = false;
    if (processesBody && route.bodyCodec == BodyCodec::webService && session.accountResyncArmed
        && !manifest_covers_account_instances(session.queuez)
        && !web_service::encode_resident_dependent_refusal(
            frame.body, scratch.responseBody, responseBodySize, staleWebAction)) {
        handled = false;
        diagnostics::report_failure(frame.serviceId, "stale_refusal");
    }
    if (staleWebAction) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap stage=web_service result=refuse reason=stale_manifest");
    }
    // Pure one-way services consume only the authenticated receive nonce.
    if (!staleWebAction && processesBody
        && !body::process(route,
                          session.queuez,
                          session.activity,
                          session.activityRosterDecode,
                          session.matchmakingContext,
                          frame.body,
                          scratch.responseBody,
                          responseBodySize,
                          outcome)) {
        diagnostics::report_failure(frame.serviceId, "body");
        // A reply-mode service answers with an empty body instead of not at all. The Client
        // matches only the head of its pending ring. One unanswered request jams that ring for
        // good, and every later reply is rejected, which is worse than a thin reply.
        clear_prefix(scratch.responseBody, responseBodySize);
        responseBodySize = 0;
        outcome = {};
        handled = sendsReply;
    }
    if (handled) {
        report_service_traffic(frame, route, responseBodySize);
    }
    if (handled && sendsReply) {
        handled = reply::encode(scratch,
                                route,
                                frame.taskId,
                                session.sessionKey,
                                session.sendNonce,
                                std::span(scratch.responseBody).first(responseBodySize),
                                framedSize);
        if (!handled) {
            diagnostics::report_failure(frame.serviceId, "encode");
        }
    }
    // Stage every requested frame and check for caller room before committing State or the nonce.
    auto nextSendNonce = session.sendNonce;
    if (handled && sendsReply) {
        middleware::secure_channel::advance_nonce(nextSendNonce);
    }
    queuez::StagedPublication queuezPublication{};
    if (handled) {
        const std::uint64_t now = GetTickCount64();
        if (now >= session.acquisitionPresentationUntilTick) {
            session.acquisitionPresentationRows = {};
            session.acquisitionPresentationRowCount = 0;
        }
        const auto acquisitionPresentationRows =
            std::span(session.acquisitionPresentationRows)
                .first(session.acquisitionPresentationRowCount);
        const bool preserveAcquisitionPresentation = now < session.acquisitionPresentationUntilTick;
        handled = queuez::stage_service_outcome(scratch,
                                                session.queuez,
                                                outcome,
                                                preserveAcquisitionPresentation,
                                                acquisitionPresentationRows,
                                                session.sessionKey,
                                                nextSendNonce,
                                                scratch.framed,
                                                framedSize,
                                                queuezPublication);
        if (handled && queuezPublication.hasState) {
            nextQueuez = queuezPublication.after;
            publishesQueuez = true;
        }
        if (!handled) {
            diagnostics::report_failure(frame.serviceId, "stage");
        }
    }
    const auto* activityPlan = transaction_if<activity_message::ActivityPlan>(outcome);
    const bool publishesFamily4 =
        publishesQueuez
        && (queuezPublication.after.family4Active != session.queuez.family4Active
            || queuezPublication.after.family4Version != session.queuez.family4Version);
    if (handled && activityPlan != nullptr) {
        handled = route.responseMode == ResponseMode::uncorrelatedPush;
        if (!handled) {
            diagnostics::report_failure(frame.serviceId, "route");
        } else if (!activity_transaction::stage_notifications(session,
                                                              scratch,
                                                              *activityPlan,
                                                              session.sessionKey,
                                                              nextSendNonce,
                                                              scratch.framed,
                                                              framedSize)) {
            // The transaction still commits. A push that cannot be built is one lost message, and
            // dropping the commit with it would strand the client's reported state for the session.
            diagnostics::report_failure(frame.serviceId, "notify");
        }
    }
    const bool artifactPurchase = transaction_if<ArtifactPurchaseTransaction>(outcome) != nullptr;
    const bool mutatesAccount =
        outcome.hasSelectCharacter || outcome.hasRecordClaim || outcome.hasArtifactReset
        || transaction_if<EquipmentSwapTransaction>(outcome) != nullptr
        || transaction_if<SubclassSelectionTransaction>(outcome) != nullptr
        || transaction_if<SocketPlugTransaction>(outcome) != nullptr
        || transaction_if<ItemStateTransaction>(outcome) != nullptr
        || artifactPurchase
        || transaction_if<CurrentActivityTransaction>(outcome) != nullptr
        || transaction_if<ItemAcquisitionTransaction>(outcome) != nullptr
        || transaction_if<ProfileItemAcquisitionTransaction>(outcome) != nullptr
        || transaction_if<ItemDismantleTransaction>(outcome) != nullptr
        || transaction_if<RecordRewardGrantTransaction>(outcome) != nullptr
        || transaction_if<SeasonPassRewardTransaction>(outcome) != nullptr
        || transaction_if<state::PendingSettingsUpdate>(outcome) != nullptr;
    const bool presentsAcquisition =
        transaction_if<ItemAcquisitionTransaction>(outcome) != nullptr
        || transaction_if<ProfileItemAcquisitionTransaction>(outcome) != nullptr
        || transaction_if<RecordRewardGrantTransaction>(outcome) != nullptr
        || transaction_if<SeasonPassRewardTransaction>(outcome) != nullptr;
    const bool invalidatesAcquisitionPresentation =
        outcome.hasChangeCharacter || outcome.hasSelectCharacter || outcome.hasArtifactReset
        || transaction_if<ItemDismantleTransaction>(outcome) != nullptr;
    const bool hasPrecommittedAccountAction =
        outcome.hasRecordClaim || outcome.hasSelectCharacter || outcome.hasArtifactReset;
    // Commit consumes pending payloads, so retain the connection fields first.
    const ConnectionFields connection = connection_fields(outcome);
    if (handled && processesBody) {
        // State changes become visible only after every requested frame and caller byte fit.
        // A refused commit sends nothing, so the frame's own reason is the only record of why.
        const bool fits = framedSize <= response.size();
        const char* commitReason = "none";
        handled = fits && transactions::commit(outcome, publication, commitReason);
        if (!handled) {
            diagnostics::report_failure(
                frame.serviceId, "commit", fits ? commitReason : "frame_capacity");
        }
        if (handled) {
            std::copy_n(scratch.framed.begin(), framedSize, response.begin());
            written = framedSize;
            // The caller copy finishes before connection fields are published.
            session.sendNonce = nextSendNonce;
            if (publishesQueuez) {
                session.queuez = nextQueuez;
            }
            arm_repushes(session, queuezPublication);
            if (invalidatesAcquisitionPresentation) {
                session.acquisitionPresentationRows = {};
                session.acquisitionPresentationRowCount = 0;
                session.acquisitionPresentationUntilTick = 0;
            } else if (queuezPublication.updatesAcquisitionPresentationRows) {
                session.acquisitionPresentationRows = queuezPublication.acquisitionPresentationRows;
                session.acquisitionPresentationRowCount =
                    queuezPublication.acquisitionPresentationRowCount;
                if (session.acquisitionPresentationRowCount == 0) {
                    session.acquisitionPresentationUntilTick = 0;
                }
            }
            if (presentsAcquisition && publishesQueuez) {
                bap::arm_acquisition_presentation_hold(session);
            }
            publish_connection_fields(session, publication, connection);
            record_committed_join(session, connection);
            // The caller copy is done, so what the staged roster body owes is settled here.
            push::activity::commit_staged_roster(session);
            commit_staged_advertisement(session);
            if (activityPlan != nullptr) {
                submit_committed_client_state(*activityPlan, publication);
                submit_committed_entity_slots_requested(*activityPlan);
                submit_committed_authority_reset(session, *activityPlan);
                submit_committed_authority_answer(session, *activityPlan);
            }
            // Any delivered activity notification resets the client's silence timer, so the
            // fallback keepalive is delayed. A roster-only answer is excluded: it owes neither the
            // global state nor the membership, and at once a second it would starve the keepalive.
            const bool defersKeepalive =
                activityPlan != nullptr && framedSize != 0
                && activityPlan->delivery != activity_message::Delivery::rosterNotification;
            if (defersKeepalive) {
                session.activityKeepaliveDueTick = GetTickCount64() + kActivityKeepaliveIntervalMs;
            }
            const bool resyncsCommittedAccount =
                hasPrecommittedAccountAction && !queuezPublication.hasState;
            if (resyncsCommittedAccount) {
                bap::arm_account_resync_everywhere();
            }
            const bool refreshesDerivedInvestment = artifactPurchase || outcome.hasArtifactReset;
            if (refreshesDerivedInvestment) {
                // Artifact overrides really live in Family 5. Record claims and their rewards do
                // not: their committed Family-4 replacement now rearms the client rebuild itself.
                session.artifactRefreshArmed = true;
            }
            if (artifactPurchase || outcome.hasArtifactReset) {
                session.artifactFamily4RefreshDueTick = GetTickCount64() + 100;
                session.artifactFamily4RefreshArmed = true;
            }
            if (outcome.hasArtifactReset) {
                session.artifactResetRefresh = outcome.artifactReset;
                session.artifactResetRefreshCursor = 0;
            }
            session.accountMutationPublished = mutatesAccount && !resyncsCommittedAccount;
            if (publishesFamily4) {
                // The Family-4 store is updated in place, so pointer identity cannot detect its
                // initial population or later revisions. Carry the exact committed publication
                // across to the next native lookup, after the client has consumed this frame.
                client::hooks::network::investment::notify_family4_publication();
            }
        }
    }
    if (!handled) {
        if (hasPrecommittedAccountAction) {
            bap::arm_account_resync_everywhere();
        }
        // The staged body is dropped, so its grant and its state byte go back for the next push.
        push::activity::discard_staged_roster(session);
        discard_staged_advertisement(session);
    }
    clear_prefix(scratch.plaintext, plaintextSize);
    clear_prefix(scratch.responseBody, responseBodySize);
    clear_prefix(scratch.framed, framedSize);
    outcome = {};
    SecureZeroMemory(&publication, sizeof publication);
    SecureZeroMemory(&queuezPublication, sizeof queuezPublication);
    return handled;
}

} // namespace sunrise::server::bap::encrypted
