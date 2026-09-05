#include "activity_message_route.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "../../../../core/logging/log.h"
#include "../../../../core/settings/settings.h"
#include "../../../../client/hooks/bootflow/bootflow_hook_lifecycle.h"
#include "../../../../middleware/bap/activity_message/activity_client_identity_parser.h"
#include "../../../../middleware/bap/activity_message/activity_join_request_parser.h"
#include "../../../../middleware/bap/activity_message/activity_membership_acknowledgement_parser.h"
#include "../../../../middleware/bap/activity_message/activity_message_request_parser.h"
#include "../../../../middleware/bap/activity_message/activity_state_refresh_parser.h"
#include "../../../../middleware/bap/activity_message/cinematic_incident.h"
#include "../../../../middleware/bap/activity_message/client_authoritative_data.h"
#include "../../../../middleware/bap/activity_message/entity_authority.h"
#include "../../../../middleware/bap/activity_message/entity_slots.h"
#include "../../../../middleware/bap/activity_message/incident.h"
#include "../../../../middleware/bap/activity_message/peer_ledger.h"
#include "../../../../middleware/bap/activity_message/player_trigger_incident.h"
#include "../../../../middleware/bap/activity_message/sense_update.h"
#include "../../../../middleware/bap/activity_message/start_activity.h"
#include "../../../../middleware/bap/activity_message/telemetry.h"
#include "../../../../middleware/bap/activity_message/wire_schema/activity_communication_route.h"
#include "../../../../middleware/bap/activity_message/wire_schema/activity_wire_schema.h"
#include "../../../../middleware/crypto/hmac.h"
#include "../../../../middleware/crypto/random_bytes.h"
#include "../../../../middleware/encoding/byte_order.h"
#include "../../../../state/activity/bubble_authority/runtime.h"
#include "../../../../state/activity/membership/activity_membership_query.h"
#include "../../../../state/activity/receipts/activity_receipts.h"
#include "../../../../state/activity/runtime.h"
#include "../../../../state/activity_sdk/runtime.h"
#include "../../../activity/host_runtime.h"
#include "../../../gameplay/gameplay_advertisement.h"
#include "../push/activity/activity_arrival.h"
#include "../push/activity/internal.h"
#include "membership/activity_membership_route.h"
#include "middleware/bap/activity_message/activity_entity_slot_request_parser.h"
#include "patch_epoch/activity_patch_epoch_route.h"
#include "receipts/activity_message_receipts.h"

namespace sunrise::server::bap::encrypted::activity_message {
namespace {

namespace service = middleware::bap::activity_message;
namespace authority = middleware::bap::activity_message::entity_authority;
namespace store = state::activity::receipts;
namespace wire_schema = middleware::bap::activity_message::wire_schema;
namespace communication = wire_schema::communication;
namespace sense_update = middleware::bap::activity_message::sense_update;
namespace cinematic_incident = middleware::bap::activity_message::cinematic_incident;
namespace player_trigger_incident = middleware::bap::activity_message::player_trigger_incident;
namespace activity_sdk = state::activity_sdk;
using IngressAdapter = communication::IngressAdapter;

/** Asks for the membership snapshot as it stands, naming no bubble. */
constexpr std::uint32_t kCurrentRevision = 0;
constexpr std::int32_t kNoBubble = -1;
/** Process-private HMAC key width used only for run-local diagnostic correlation. */
constexpr std::size_t kFingerprintKeySize = 32;
/** Domain prefix keeps this diagnostic use separate from protocol authentication. */
constexpr std::array<std::byte, 4> kFingerprintDomain{
    std::byte{'A'}, std::byte{'H'}, std::byte{'I'}, std::byte{'1'}};

/** One process-private key, unavailable when system entropy failed. */
struct FingerprintKey final {
    std::array<std::byte, kFingerprintKeySize> bytes{};
    bool available{};
};

/** One non-reversible, run-local body correlation value. */
struct Fingerprint final {
    std::uint64_t value{};
    bool present{};
};

/** Diagnostic facts kept separately from the aggregate receipt verdict. */
struct DiagnosticBody final {
    const state::activity::membership::AuthoritativeUpdate* authoritative{};
    const sense_update::DecodedPacket* sense{};
    std::size_t consumedBits{};
    server::activity::host::ClientMessageStatus status{
        server::activity::host::ClientMessageStatus::unclassified};
};

/** Borrowed exact connection and SDK state used only during one msg-6 route call. */
struct SenseResolverContext final {
    const ActivityClientBinding* binding{};
    const RosterDecodeMap* roster{};
    const activity_sdk::Catalog* catalog{};
};

/** Resolves the group one sense registry key names, against this connection's roster. */
[[nodiscard]] sense_update::TargetStatus resolve_sense_group(
    const void* raw, std::uint32_t registryKey, sense_update::GroupTarget& output) noexcept {
    output = {};
    const auto* const context = static_cast<const SenseResolverContext*>(raw);
    if (context == nullptr || context->binding == nullptr || context->roster == nullptr
        || context->catalog == nullptr) {
        return sense_update::TargetStatus::targetUnavailable;
    }
    const RosterDecodeEntry* const entry = find_roster_decode_entry(
        *context->roster, context->binding->bindingGeneration, registryKey);
    if (entry == nullptr || entry->objectTag == 0) {
        return sense_update::TargetStatus::targetUnavailable;
    }
    const auto objects = context->catalog->objects();
    const activity_sdk::format::Object* match = nullptr;
    for (const activity_sdk::format::Object& object : objects) {
        if (object.objectTag != entry->objectTag) {
            continue;
        }
        if (match != nullptr) {
            return sense_update::TargetStatus::targetUnavailable;
        }
        match = &object;
    }
    if (match == nullptr || match->objectKey != registryKey) {
        return sense_update::TargetStatus::targetUnavailable;
    }
    output.objectTag = entry->objectTag;
    output.objectRow = static_cast<std::uint32_t>(match - objects.data());
    return sense_update::TargetStatus::resolved;
}

[[nodiscard]] sense_update::TargetStatus
resolve_sense_slot(const void* raw,
                   const sense_update::GroupTarget& group,
                   std::uint8_t slotType,
                   std::uint16_t slotIndex,
                   sense_update::SlotTarget& output) noexcept {
    output = {};
    const auto* const context = static_cast<const SenseResolverContext*>(raw);
    if (context == nullptr || context->catalog == nullptr
        || group.objectRow >= context->catalog->objects().size()) {
        return sense_update::TargetStatus::targetUnavailable;
    }
    const activity_sdk::format::Object& object = context->catalog->objects()[group.objectRow];
    if (object.objectTag != group.objectTag) {
        return sense_update::TargetStatus::targetUnavailable;
    }
    const auto allSlots = context->catalog->slots();
    const activity_sdk::format::Slot* match = nullptr;
    for (const activity_sdk::format::Slot& slot :
         activity_sdk::object_slots(*context->catalog, object)) {
        if (slot.slotType != slotType || slot.slotIndex != slotIndex) {
            continue;
        }
        if (match != nullptr) {
            return sense_update::TargetStatus::targetUnavailable;
        }
        match = &slot;
    }
    if (match == nullptr || match->objectIndex != group.objectRow) {
        return sense_update::TargetStatus::targetUnavailable;
    }
    output.slotRow = static_cast<std::uint32_t>(match - allSlots.data());
    output.senseSchema = match->senseSchema;
    if (match->componentClass == activity_sdk::format::kAbsentIndex || match->senseSchema == 0
        || match->senseSchema == activity_sdk::format::kAbsentIndex
        || (match->flags & activity_sdk::format::kSlotSchemaJoinExact) == 0) {
        return sense_update::TargetStatus::schemaUnavailable;
    }
    // Native Sense codecs dispatch by the authored schema handle. No SDK reflection row exists.
    output.schemaRow = match->senseSchema;
    return sense_update::TargetStatus::resolved;
}

/** @return Process-private fingerprint key, initialized once from Windows system entropy. */
[[nodiscard]] const FingerprintKey& fingerprint_key() noexcept {
    static const FingerprintKey key = []() noexcept {
        FingerprintKey value{};
        value.available = middleware::crypto::random::fill(value.bytes);
        return value;
    }();
    return key;
}

/** @return Keyed, run-local correlation value without retaining the borrowed payload. */
[[nodiscard]] Fingerprint payload_fingerprint(std::uint32_t messageType,
                                              std::span<const std::byte> payload) noexcept {
    const FingerprintKey& key = fingerprint_key();
    if (!key.available) {
        return {};
    }
    std::array<std::byte, kFingerprintDomain.size() + middleware::encoding::kU32Size> domain{};
    std::copy(kFingerprintDomain.begin(), kFingerprintDomain.end(), domain.begin());
    middleware::encoding::write_u32_be(
        std::span(domain).subspan<kFingerprintDomain.size(), middleware::encoding::kU32Size>(),
        messageType);
    middleware::crypto::hmac::Digest digest{};
    if (!middleware::crypto::hmac::authenticate(
            middleware::crypto::hmac::Algorithm::sha256, key.bytes, domain, payload, digest)
        || digest.size < middleware::encoding::kU64Size) {
        return {};
    }
    Fingerprint result{};
    result.value = middleware::encoding::read_u64_be(
        std::span(digest.bytes).first<middleware::encoding::kU64Size>());
    result.present = true;
    return result;
}

/** @return Diagnostic body status implied by one framing-only parser result. */
[[nodiscard]] server::activity::host::ClientMessageStatus
diagnostic_status(const receipts::Framed& framed, bool incident) noexcept {
    using Status = server::activity::host::ClientMessageStatus;
    switch (framed.verdict) {
    case store::Verdict::framed:
        return incident ? Status::outerDecoded : Status::decoded;
    case store::Verdict::partial:
        return framed.consumedBits == 0 ? Status::opaque : Status::prefixOnly;
    case store::Verdict::malformed:
        return Status::malformed;
    case store::Verdict::quarantined:
        return Status::quarantined;
    case store::Verdict::absent:
    case store::Verdict::unowned:
        return Status::unclassified;
    }
    return Status::unclassified;
}

/**
 * Reports one inbound activity message, whatever the route goes on to do with it.
 * Without this line a type the client never sends reads the same as one handled in silence.
 * Nothing else says whether the client ever asks for or returns an entity slot.
 * @param request Parsed envelope.
 */
void report_arrival(const service::Request& request) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=inbound type=%u handle=0x%llX bytes=%zu",
                                      request.messageType,
                                      static_cast<unsigned long long>(request.sessionId),
                                      request.payload.size());
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Reports one activity message the route did not stage, naming its type.
 * Every inbound activity message is one-way, so nothing here can jam the client's reply ring. An
 * unnamed drop is invisible, and membership waits on the identity message.
 * @param messageType Activity message type from the envelope.
 * @param sessionId Activity session the envelope named.
 * @param reason Short name of the step that declined.
 */
void report_message(std::uint32_t messageType,
                    std::uint64_t sessionId,
                    const char* reason) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=message result=skip type=%u "
                                      "handle=0x%llX reason=%s",
                                      messageType,
                                      static_cast<unsigned long long>(sessionId),
                                      reason);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Records one arrival in the aggregate receipts and the owned diagnostic history.
 * @param ownedBinding Exact owner, or null when the message was not owned.
 * @param zeroHandleOwned True only for retail msg52 after its current link binding was proved.
 */
[[nodiscard]] std::uint64_t record(const service::Request& request,
                                   store::Verdict receiptVerdict,
                                   std::size_t receiptConsumedBits,
                                   const state::activity::SessionBinding* ownedBinding,
                                   std::uint64_t sourceGeneration,
                                   const DiagnosticBody& diagnostic = {},
                                   bool zeroHandleOwned = false) noexcept {
    std::uint64_t sequence = 0;
    const bool exactEnvelopeOwner =
        ownedBinding != nullptr && ownedBinding->sessionId == request.sessionId;
    const bool exactZeroHandleOwner =
        ownedBinding != nullptr && zeroHandleOwned && request.sessionId == 0;
    if (exactEnvelopeOwner || exactZeroHandleOwner) {
        const Fingerprint fingerprint = payload_fingerprint(request.messageType, request.payload);
        server::activity::host::ClientMessageInput input{};
        input.binding = *ownedBinding;
        input.sourceGeneration = sourceGeneration;
        input.payloadFingerprint = fingerprint.value;
        input.messageType = request.messageType;
        input.payloadBytes = static_cast<std::uint32_t>(request.payload.size());
        input.peerHeardMask = request.peerHeardMask;
        input.consumedBits = static_cast<std::uint32_t>(diagnostic.consumedBits);
        input.status = diagnostic.status;
        input.hasPayloadFingerprint = fingerprint.present;
        if (diagnostic.authoritative != nullptr) {
            input.authoritative = *diagnostic.authoritative;
            input.hasAuthoritative = true;
        }
        communication::ActivityCommunicationRoute route{};
        const bool executableRoute =
            activity_sdk::executable_communication_route(request.messageType, route);
        // Message handlers parse their own authored native body. Routing retains no parallel
        // schema-driven scalar decode.
        sequence = server::activity::host::record_client_message(input, diagnostic.sense);
        if (sequence != 0 && request.messageType != service::entity_slot_request::kMessageType
            && executableRoute
            && route.ingressDelivery == communication::IngressDeliveryPolicy::protocolHostInput
            && !server::activity::host::submit_client_message(input, sequence)) {
            report_message(request.messageType, request.sessionId, "mission_ingress_refused");
        }
    }
    if (!core::settings::get().server.activation.activityCompatibilityMirror) {
        // Off, only the aggregate receipt row is skipped. The diagnostic history stays live.
        return sequence;
    }
    store::Arrival arrival{};
    arrival.sessionId = request.sessionId;
    arrival.messageType = request.messageType;
    arrival.payloadBytes = static_cast<std::uint32_t>(request.payload.size());
    arrival.peerHeardMask = request.peerHeardMask;
    arrival.consumedBits = static_cast<std::uint32_t>(receiptConsumedBits);
    arrival.verdict = receiptVerdict;
    static_cast<void>(store::record(arrival));
    return sequence;
}

/** Tests whether a retained link binding still names its exact State and host generations. */
[[nodiscard]] bool binding_is_current(const ActivityClientBinding& binding) noexcept {
    if (binding.role == ActivityClientRole::privateCurrent) {
        return binding.session.sessionId != state::activity::kAbsentSessionId
               && binding.source.sessionId == binding.session.sessionId
               && binding.source.createdRevision == binding.session.createdRevision
               && state::activity::binding_matches(binding.session);
    }
    if (binding.role != ActivityClientRole::publicTarget
        || !state::activity::binding_matches(binding.session)
        || !state::activity::binding_matches(binding.source)) {
        return false;
    }
    server::gameplay::group::HostSessionBinding host{};
    return server::gameplay::group::host_session_for_activity(binding.session.sessionId, host)
           && host.generation == binding.hostGeneration
           && host.groupSessionId == binding.groupSessionId
           && host.source.sessionId == binding.source.sessionId
           && host.source.createdRevision == binding.source.createdRevision
           && host.target.sessionId == binding.session.sessionId
           && host.target.createdRevision == binding.session.createdRevision;
}

/**
 * Tests whether one message may mutate the State this link owns.
 * A mutating body names its session through the envelope handle. Join and patch-epoch messages
 * have separate ownership rules and do not use this helper.
 * @param binding Exact ActivityClient generation owned by this link.
 * @param request Validated envelope.
 * @return True when the envelope handle may drive a State mutation.
 */
[[nodiscard]] bool owns_session(const ActivityClientBinding& binding,
                                const service::Request& request) noexcept {
    return binding_is_current(binding) && request.sessionId == binding.session.sessionId;
}

/**
 * Prepares the joined State and the whole initial lease mask as one mutation.
 * @param binding Exact ActivityClient generation already owned by this link.
 * @param request Validated owned svc8 envelope.
 * @param plan Cleared, then receives join scalars and the chosen lease mask.
 * @return True when the fixed join payload and current State can stage together.
 */
[[nodiscard]] bool prepare_join(const ActivityClientBinding& binding,
                                const service::Request& request,
                                ActivityPlan& plan) noexcept {
    service::JoinRequest parsed;
    if (!service::join_request::parse_join_request(request.payload, parsed)
        || parsed.sessionId != request.sessionId) {
        return false;
    }
    if (binding_is_current(binding) && parsed.sessionId == binding.session.sessionId) {
        plan.bindingIntent = BindingIntent::preserveCurrent;
        plan.targetBinding = binding.session;
        // The join burst carries the membership body, so its logical host must already be ready.
        // Private activities use one stable Bubble Host. Public regions use their citizen host.
        const std::int32_t arrival = push::activity::effective_region(binding.session).index;
        if (push::activity::private_region(binding.session, binding.bindingGeneration, arrival)) {
            if (!server::gameplay::complete_private_host_session(binding.session, arrival)) {
                return false;
            }
        } else {
            server::gameplay::complete_host_session(binding.session, arrival);
        }
    } else if (server::gameplay::group::host_session_for_activity(parsed.sessionId, plan.publicHost)
               && state::activity::binding_matches(plan.publicHost.target)) {
        plan.bindingIntent = BindingIntent::publicTarget;
        plan.targetBinding = plan.publicHost.target;
    } else {
        return false;
    }
    // The client takes the low slots and the server keeps the reserve above them.
    const core::settings::server::gameplay::Settings& gameplay =
        core::settings::get().server.gameplay;
    const std::size_t reserve = core::settings::server::gameplay::effective_reserve(gameplay);
    const std::size_t granted = core::settings::server::gameplay::join_grant(gameplay);
    if (!state::activity::entity_slots::prepare_join(
            parsed.sessionId, parsed.memberKey, granted, reserve, plan.entitySlotMutation)) {
        return false;
    }
    plan.correlation = parsed.correlation;
    plan.sessionId = parsed.sessionId;
    plan.joinCharacterSoid = parsed.characterSoid;
    plan.delivery = Delivery::joinNotifications;
    plan.mutationDomain = MutationDomain::entitySlots;
    // Read, never committed: the domain above is what the commit acts on, so this cannot move the
    // private session's membership revision. The body is that session's member table, sent
    // verbatim, because only it names a member the client recognises as the local player.
    if (plan.bindingIntent == BindingIntent::publicTarget
        && state::activity::binding_matches(plan.publicHost.source)) {
        static_cast<void>(
            state::activity::membership::prepare_refresh(plan.publicHost.source.sessionId,
                                                         kCurrentRevision,
                                                         kNoBubble,
                                                         plan.membershipMutation));
    } else if (plan.bindingIntent == BindingIntent::preserveCurrent) {
        // A private join's burst carries the seed membership; the commit lands the same seed.
        static_cast<void>(push::activity::prepare_join_seed_snapshot(
            plan.targetBinding.createdRevision,
            parsed.memberKey,
            parsed.characterSoid,
            plan.membershipMutation));
    }
    return true;
}

/**
 * Prepares only currently free slots for one positive client request.
 * The ask is a floor, not the amount. The client requests the slots one slice set needs only once
 * it has begun creating that slice set's entities, so a grant sized to the ask arrives after the
 * creates it was meant to cover have already failed. Topping the lease up to a standing high
 * water instead leaves the slots held before the next switch starts. `prepare_grant` picks from
 * the free complement and documents that an ask above the slot count degrades to every remaining
 * free slot, so an over-large top-up cannot fail a request that would otherwise have succeeded.
 * @param request Validated owned svc8 envelope.
 * @param plan Cleared, then receives the chosen lease mask.
 * @return True for a valid positive request, including an exhausted zero-mask grant.
 */
[[nodiscard]] bool prepare_grant(const service::Request& request, ActivityPlan& plan) noexcept {
    std::int32_t requested = 0;
    if (!service::entity_slot_request::parse_entity_slot_request(request.payload, requested)
        || requested <= 0) {
        return false;
    }
    std::size_t wanted = static_cast<std::size_t>(requested);
    const std::size_t highWater = core::settings::server::gameplay::lease_high_water(
        core::settings::get().server.gameplay);
    std::size_t held = 0;
    std::size_t reserved = 0;
    // A session with no readable lease keeps the client's own ask, which is today's behaviour.
    if (state::activity::entity_slots::lease_counts(request.sessionId, held, reserved)
        && held < highWater) {
        wanted = (std::max)(wanted, highWater - held);
    }
    if (!state::activity::entity_slots::prepare_grant(
            request.sessionId, wanted, plan.entitySlotMutation)) {
        return false;
    }
    // The lease line downstream reports the topped-up count, so without this the size of the
    // client's own ask — the thing that says which slice set it is about to build — is lost.
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=lease_topup soid=0x%llX asked=%d "
                                      "wanted=%zu held=%zu high_water=%zu",
                                      static_cast<unsigned long long>(request.sessionId),
                                      requested,
                                      wanted,
                                      held,
                                      highWater);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    plan.sessionId = request.sessionId;
    plan.entitySlotsRequested.requestedCount = requested;
    plan.delivery = Delivery::entitySlotNotification;
    plan.mutationDomain = MutationDomain::entitySlots;
    return true;
}

/**
 * Prepares only the slots that are both held and in the returned mask.
 * @param request Validated owned svc8 envelope.
 * @param plan Cleared, then receives the chosen release mask.
 * @return True when the exact mask decodes and its session can stage a release.
 */
[[nodiscard]] bool prepare_release(const service::Request& request, ActivityPlan& plan) noexcept {
    service::entity_slots::EntitySlotMask decoded{};
    if (!service::entity_slots::decode_entity_slots(request.payload, decoded)) {
        return false;
    }
    if (core::settings::get().server.gameplay.ignoreClientSlotRelease) {
        // Framed and receipted as before; only the lease is left standing. The mask's meaning is
        // unproven, and reading it the wrong way round shrinks the lease the client's own entity
        // bitmap mirrors, which starves entity creation.
        plan.sessionId = request.sessionId;
        plan.delivery = Delivery::none;
        plan.mutationDomain = MutationDomain::none;
        return true;
    }
    state::activity::entity_slots::LeaseMask returned{};
    std::copy(decoded.begin(), decoded.end(), returned.begin());
    if (!state::activity::entity_slots::prepare_release(
            request.sessionId, returned, plan.entitySlotMutation)) {
        return false;
    }
    plan.sessionId = request.sessionId;
    plan.delivery = Delivery::none;
    plan.mutationDomain = MutationDomain::entitySlots;
    return true;
}

/** One framing-only adapter and the handler that reads its body. */
struct FramingRoute {
    IngressAdapter adapter;
    receipts::Framed (*frame)(const service::Request&) noexcept;
};

/**
 * Drops the recorded grant for the bubble one release names, but only once the client has left it.
 * The receipts module reports without touching State by design, so the State change a hand-back
 * implies is made here. Without it the bubble stays recorded as granted for the rest of the
 * session and re-entering it — which is what every wipe, retry and backtrack does — runs with no
 * authority, because `select_grant` only ever grants a bubble whose token is zero.
 *
 * The occupancy test is what makes this safe on the common path. Msg 26 is documented as the
 * bubble exit, but msg 33 gives up *a set of slots* and the client sends it without leaving.
 * Clearing the token while the player is still inside would let the next roster push — one every
 * second during the load burst — re-grant the occupied bubble under a new token, on every
 * destination rather than only this raid. Comparing the selector against the region the client
 * last reported keeps the release to a real exit; a session that has reported no region yet
 * cannot be judged, so it is left alone.
 *
 * The test is deliberately fail-safe rather than exact. `reported_region` lags a boundary
 * crossing, so a release sent the instant the client leaves can still name the region it is
 * leaving and be skipped. That loses a re-arm, which is the behaviour before this change; it
 * never clears a bubble the player occupies, which would be worse than that behaviour.
 * The selector is the raw bubble index, not a biased field: the captured releases carry 1, 14
 * and 12, matching dream_shore, raid_larceny_staging and raid_larceny_alarm — the three bubbles
 * that run actually visited.
 * @param request Validated owned activity envelope carrying the release.
 * @param expectReason True for abandon, which trails a reason after the mask.
 */
void release_named_bubble(const service::Request& request, bool expectReason) noexcept {
    authority::Release decoded{};
    const bool parsed = expectReason ? authority::parse_abandon(request.payload, decoded)
                                     : authority::parse_abdicate(request.payload, decoded);
    if (!parsed || decoded.selector >= state::activity::bubble_authority::kFallbackBubble) {
        return;
    }
    const std::int32_t region =
        state::activity::membership::reported_region(request.sessionId);
    if (region < 0) {
        return;
    }
    const auto occupied = static_cast<std::uint8_t>(
        region >> state::activity::bubble_authority::kSliceSetToBubbleShift);
    if (decoded.selector == occupied) {
        return;
    }
    state::activity::bubble_authority::release_grant(request.sessionId, decoded.selector);
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=authority result=released type=%u "
                                      "selector=%u occupied=%u",
                                      request.messageType,
                                      static_cast<unsigned>(decoded.selector),
                                      static_cast<unsigned>(occupied));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** Frames one abandon, which trails a reason after the mask. */
[[nodiscard]] receipts::Framed frame_abandon(const service::Request& request) noexcept {
    release_named_bubble(request, true);
    return receipts::frame_authority_release(request, true);
}

/** Frames one abdicate, which carries no reason. */
[[nodiscard]] receipts::Framed frame_abdicate(const service::Request& request) noexcept {
    release_named_bubble(request, false);
    return receipts::frame_authority_release(request, false);
}

/**
 * Every adapter this route frames and records.
 * All of these are read-only except the two authority releases, which drop the grant token for a
 * bubble the client has left so it can be granted again on re-entry.
 */
constexpr std::array<FramingRoute, 19> kFramingRoutes{{
    {IngressAdapter::routeMisuseReceipt, receipts::frame_route_misuse},
    {IngressAdapter::reservationRequest, receipts::frame_reservation_request},
    {IngressAdapter::reservationRelease, receipts::frame_reservation_release},
    {IngressAdapter::peerLeave, receipts::frame_peer_leave},
    {IngressAdapter::clientKeepalive, receipts::frame_client_keepalive},
    {IngressAdapter::incidentHostIncident, receipts::frame_incident},
    {IngressAdapter::authorityAbandon, frame_abandon},
    {IngressAdapter::authorityAbdicate, frame_abdicate},
    {IngressAdapter::authorityRequestPurge, receipts::frame_request_purge},
    {IngressAdapter::authorityResetAcknowledgement, receipts::frame_query_answer},
    {IngressAdapter::authorityQueryAnswer, receipts::frame_query_answer},
    {IngressAdapter::debugCommandQuarantine, receipts::frame_debug_command},
    {IngressAdapter::connectivityFailure, receipts::frame_connectivity_failure},
    {IngressAdapter::heartbeat, receipts::frame_heartbeat},
    {IngressAdapter::opaqueScalar, receipts::frame_opaque_scalar},
    {IngressAdapter::lagSwitch, receipts::frame_lag_switch},
    {IngressAdapter::connectionQuality, receipts::frame_connection_quality},
    {IngressAdapter::migration, receipts::frame_migration},
    {IngressAdapter::highWater, receipts::frame_high_water},
}};

/**
 * Frames one message and records its receipt.
 * Read-only except for the two authority releases, which clear a departed bubble's grant token.
 * @param request Validated envelope.
 * @return Always true: a framing-only message can never fail the transport frame.
 */
[[nodiscard]] bool frame_only(const ActivityClientBinding& binding,
                              const RosterDecodeMap& rosterDecode,
                              IngressAdapter adapter,
                              const service::Request& request) noexcept {
    const auto row = std::find_if(
        kFramingRoutes.begin(),
        kFramingRoutes.end(),
        [adapter](const FramingRoute& candidate) noexcept { return candidate.adapter == adapter; });
    service::incident::Incident parsedIncident{};
    sense_update::SenseUpdate parsedSense{};
    const bool isIncident = adapter == IngressAdapter::incidentHostIncident;
    const bool isSense = adapter == IngressAdapter::senseUpdateHostSense;
    receipts::Framed framed{};
    if (isSense) {
        const activity_sdk::Snapshot catalog = activity_sdk::snapshot();
        const SenseResolverContext context{&binding, &rosterDecode, catalog.get()};
        const sense_update::Resolver resolver{&context, resolve_sense_group, resolve_sense_slot};
        std::size_t consumedBits = 0;
        static_cast<void>(sense_update::decode_sense_update(
            request.payload, resolver, parsedSense, consumedBits));
        framed = receipts::frame_sense_update(request, parsedSense);
    } else {
        framed = isIncident ? receipts::frame_incident_copy(request, parsedIncident)
                 : row != kFramingRoutes.end() ? row->frame(request)
                                               : receipts::frame_unknown(request);
    }
    DiagnosticBody diagnostic{};
    diagnostic.consumedBits = framed.consumedBits;
    diagnostic.status = diagnostic_status(framed, isIncident);
    diagnostic.sense = isSense ? &parsedSense.decoded : nullptr;
    if (isSense && parsedSense.decoded.status == sense_update::DecodeStatus::partial) {
        diagnostic.status = server::activity::host::ClientMessageStatus::decodedPartial;
    }
    const std::uint64_t clientMessageSequence = record(request,
                                                       framed.verdict,
                                                       framed.consumedBits,
                                                       &binding.session,
                                                       binding.bindingGeneration,
                                                       diagnostic);
    if (isSense && parsedSense.decoded.status != sense_update::DecodeStatus::malformed) {
        server::activity::host::SenseInput input{};
        input.binding = binding.session;
        input.sourceGeneration = binding.bindingGeneration;
        input.clientMessageSequence = clientMessageSequence;
        input.epochFirst = parsedSense.epoch.first;
        input.epochSecond = parsedSense.epoch.second;
        input.payloadBytes = static_cast<std::uint32_t>(request.payload.size());
        input.peerHeardMask = request.peerHeardMask;
        input.tailBits = parsedSense.tailBits;
        input.consumedBits = static_cast<std::uint32_t>(framed.consumedBits);
        input.firstGroupBits = parsedSense.firstGroupBits;
        input.firstRegistryKey = parsedSense.firstRegistryKey;
        input.groupsSeen = parsedSense.decoded.groupsSeen;
        input.groupsDecoded = parsedSense.decoded.groupsDecoded;
        input.groupsSkipped = parsedSense.decoded.groupsSkipped;
        input.objectsSeen = parsedSense.decoded.objectsSeen;
        input.objectsDecoded = parsedSense.decoded.objectsDecoded;
        input.firstSlotIndex = parsedSense.firstSlotIndex;
        input.firstSlotType = parsedSense.firstSlotType;
        input.decodeStatus = parsedSense.decoded.status;
        input.verdict = framed.verdict;
        input.decoded = parsedSense.decoded;
        input.hasFirstObject = parsedSense.hasFirstObject;
        if (!server::activity::host::submit_sense(input)) {
            report_message(request.messageType, request.sessionId, "host_ingress_refused");
        }
    } else if (isIncident && framed.verdict == store::Verdict::framed) {
        server::activity::host::IncidentInput input{};
        input.binding = binding.session;
        input.incident = parsedIncident;
        input.sourceGeneration = binding.bindingGeneration;
        input.clientMessageSequence = clientMessageSequence;
        input.payloadBytes = static_cast<std::uint32_t>(request.payload.size());
        if (parsedIncident.primaryTarget == player_trigger_incident::kPrimaryTarget
            && parsedIncident.payloadLength == player_trigger_incident::kPayloadBytes) {
            input.hasPlayerTrigger = player_trigger_incident::decode(
                std::span(parsedIncident.payload).first(parsedIncident.payloadLength),
                input.playerTrigger);
        }
        if (parsedIncident.payloadLength == cinematic_incident::kPayloadBytes
            && cinematic_incident::signal_for_target(parsedIncident.primaryTarget,
                                                     input.cinematicSignal)) {
            input.hasCinematic = cinematic_incident::decode(
                std::span(parsedIncident.payload).first(parsedIncident.payloadLength),
                input.cinematic);
        }
        if (!server::activity::host::submit_incident(input)) {
            report_message(request.messageType, request.sessionId, "host_ingress_refused");
        }
    }
    return true;
}

/** Retains one exact msg-31 or msg-32 answer until its authenticated frame commits. */
[[nodiscard]] bool prepare_authority_query_answer(const ActivityClientBinding& binding,
                                                  const RosterDecodeMap& rosterDecode,
                                                  IngressAdapter adapter,
                                                  const service::Request& request,
                                                  ActivityPlan& plan,
                                                  bool& hasTransaction) noexcept {
    service::entity_authority::QueryAnswer answer{};
    const bool parsed =
        service::entity_authority::parse_query_answer(request.messageType, request.payload, answer);
    if (!frame_only(binding, rosterDecode, adapter, request)) {
        return false;
    }
    if (!parsed
        || (request.messageType != service::entity_authority::kQueryPerBubbleMessageType
            && request.messageType != service::entity_authority::kQueryResponseMessageType)) {
        return true;
    }
    plan.sessionId = request.sessionId;
    plan.authorityQuery.answer = answer;
    plan.authorityQuery.sourceGeneration = binding.bindingGeneration;
    plan.authorityQuery.pending = true;
    plan.delivery = Delivery::none;
    plan.mutationDomain = MutationDomain::authorityQuery;
    hasTransaction = true;
    return true;
}

/** Retains one exact msg-29 acknowledgement until its authenticated frame commits. */
[[nodiscard]] bool prepare_authority_reset_acknowledgement(const ActivityClientBinding& binding,
                                                           const RosterDecodeMap& rosterDecode,
                                                           IngressAdapter adapter,
                                                           const service::Request& request,
                                                           ActivityPlan& plan,
                                                           bool& hasTransaction) noexcept {
    service::entity_authority::QueryAnswer answer{};
    const bool parsed =
        service::entity_authority::parse_query_answer(request.messageType, request.payload, answer);
    if (!frame_only(binding, rosterDecode, adapter, request)) {
        return false;
    }
    if (!parsed
        || request.messageType != service::entity_authority::kResetAcknowledgementMessageType) {
        return true;
    }
    plan.sessionId = request.sessionId;
    plan.authorityReset.answer = answer;
    plan.authorityReset.sourceGeneration = binding.bindingGeneration;
    plan.authorityReset.pending = true;
    plan.delivery = Delivery::none;
    plan.mutationDomain = MutationDomain::authorityReset;
    hasTransaction = true;
    return true;
}

} // namespace

/** Routes one svc8 activity message and prepares any supported push transaction. */
bool process(const ActivityClientBinding& binding,
             const RosterDecodeMap& rosterDecode,
             std::span<const std::byte> requestBody,
             ActivityPlan& plan,
             bool& hasTransaction) noexcept {
    plan = {};
    hasTransaction = false;

    service::Request request;
    if (!service::parse_request(requestBody, request)) {
        report_message(0, 0, "parse");
        return false;
    }
    report_arrival(request);
    communication::ActivityCommunicationRoute route{};
    const bool executableRoute =
        activity_sdk::executable_communication_route(request.messageType, route);
    const IngressAdapter adapter = executableRoute ? route.ingressAdapter : IngressAdapter::none;
    // Join acquires a binding. Msg52 may name the current binding or use its zero-handle form.
    const bool acquiresBinding = adapter == IngressAdapter::joinRequestStateJoin;
    const bool zeroHandlePatchEpoch = adapter == IngressAdapter::patchEpochStateEpoch;
    const bool currentOwnsEnvelope = owns_session(binding, request);
    const bool ownsMessage =
        acquiresBinding
        || (zeroHandlePatchEpoch
                ? currentOwnsEnvelope || (binding_is_current(binding) && request.sessionId == 0)
                : currentOwnsEnvelope);
    if (!ownsMessage) {
        report_message(request.messageType, request.sessionId, "unowned");
        static_cast<void>(record(request, store::Verdict::unowned, 0, nullptr, 0));
        return true;
    }
    bool prepared = false;
    switch (adapter) {
    case IngressAdapter::patchEpochStateEpoch:
        prepared = patch_epoch::prepare(binding.session.sessionId, request, plan);
        break;
    case IngressAdapter::joinRequestStateJoin:
        prepared = prepare_join(binding, request, plan);
        break;
    case IngressAdapter::entitySlotRequestStateSlots:
        prepared = prepare_grant(request, plan);
        break;
    case IngressAdapter::entitySlotsStateSlots:
        prepared = prepare_release(request, plan);
        break;
    case IngressAdapter::stateRefreshMembership:
        prepared = membership::prepare_refresh(request, plan);
        break;
    case IngressAdapter::clientIdentityMembership:
        prepared = membership::prepare_identity(request, plan);
        break;
    case IngressAdapter::clientAuthoritativeDataMembership:
        prepared = membership::prepare_authoritative(request, plan);
        break;
    case IngressAdapter::membershipAcknowledgement:
        prepared = membership::prepare_acknowledgement(request, plan);
        break;
    case IngressAdapter::startActivityOptionalStateRefresh:
        // Off, the request is framed and recorded but no transition policy runs on it.
        if (!core::settings::get().server.activation.defaultClientActivation) {
            const receipts::Framed framed = receipts::frame_start_activity(request);
            DiagnosticBody diagnostic{};
            diagnostic.consumedBits = framed.consumedBits;
            diagnostic.status = diagnostic_status(framed, false);
            static_cast<void>(record(request,
                                     framed.verdict,
                                     framed.consumedBits,
                                     &binding.session,
                                     binding.bindingGeneration,
                                     diagnostic));
            return true;
        }
        prepared = membership::prepare_start_activity(request, plan);
        break;
    case IngressAdapter::authorityResetAcknowledgement:
        return prepare_authority_reset_acknowledgement(
            binding, rosterDecode, adapter, request, plan, hasTransaction);
    case IngressAdapter::authorityQueryAnswer:
        return prepare_authority_query_answer(
            binding, rosterDecode, adapter, request, plan, hasTransaction);
    default:
        return frame_only(binding, rosterDecode, adapter, request);
    }
    // A message that cannot be staged is reported and dropped. Failing the frame would leave the
    // client's pending ring jammed.
    if (!prepared) {
        report_message(request.messageType, request.sessionId, "prepare");
        const state::activity::SessionBinding* const ownedBinding =
            acquiresBinding && !currentOwnsEnvelope ? nullptr : &binding.session;
        DiagnosticBody diagnostic{};
        diagnostic.status = server::activity::host::ClientMessageStatus::prepareRefused;
        static_cast<void>(record(request,
                                 store::Verdict::malformed,
                                 0,
                                 ownedBinding,
                                 binding.bindingGeneration,
                                 diagnostic,
                                 zeroHandlePatchEpoch));
        plan = {};
        return true;
    }
    if (adapter == IngressAdapter::joinRequestStateJoin) {
        const std::size_t consumedBits =
            request.payload.size() * middleware::encoding::kBitsPerByte;
        const Fingerprint fingerprint = payload_fingerprint(request.messageType, request.payload);
        plan.joinIngress.payloadFingerprint = fingerprint.value;
        plan.joinIngress.payloadBytes = static_cast<std::uint32_t>(request.payload.size());
        plan.joinIngress.peerHeardMask = request.peerHeardMask;
        plan.joinIngress.consumedBits = static_cast<std::uint32_t>(consumedBits);
        plan.joinIngress.hasPayloadFingerprint = fingerprint.present;
        plan.joinIngress.prepared = true;
        static_cast<void>(record(request, store::Verdict::framed, consumedBits, nullptr, 0));
        hasTransaction = true;
        return true;
    }
    const state::activity::SessionBinding& ownedBinding = binding.session;
    DiagnosticBody diagnostic{};
    diagnostic.status = server::activity::host::ClientMessageStatus::prepared;
    const bool publishesClientState = adapter == IngressAdapter::clientAuthoritativeDataMembership;
    if (publishesClientState) {
        diagnostic.authoritative = &plan.membershipMutation.authoritativeInput;
        diagnostic.consumedBits = request.payload.size() * middleware::encoding::kBitsPerByte;
        diagnostic.status = server::activity::host::ClientMessageStatus::decoded;
    }
    const std::uint64_t clientMessageSequence =
        record(request,
               store::Verdict::framed,
               request.payload.size() * middleware::encoding::kBitsPerByte,
               &ownedBinding,
               binding.bindingGeneration,
               diagnostic,
               zeroHandlePatchEpoch);
    if (adapter == IngressAdapter::entitySlotRequestStateSlots && clientMessageSequence != 0) {
        plan.entitySlotsRequested.binding = ownedBinding;
        plan.entitySlotsRequested.sourceGeneration = binding.bindingGeneration;
        plan.entitySlotsRequested.clientMessageSequence = clientMessageSequence;
        plan.entitySlotsRequested.pending = true;
    }
    if (publishesClientState && clientMessageSequence != 0) {
        plan.clientState.binding = ownedBinding;
        plan.clientState.sourceGeneration = binding.bindingGeneration;
        plan.clientState.clientMessageSequence = clientMessageSequence;
        plan.clientState.payloadBytes = static_cast<std::uint32_t>(request.payload.size());
        plan.clientState.pending = true;
    }
    hasTransaction = true;
    return true;
}

} // namespace sunrise::server::bap::encrypted::activity_message
