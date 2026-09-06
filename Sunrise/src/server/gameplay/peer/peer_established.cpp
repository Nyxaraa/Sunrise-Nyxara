#include <Windows.h>

#include <array>

#include "../../../middleware/encoding/bit_reader.h"
#include "../../../middleware/encoding/bit_writer.h"
#include "../../../middleware/gameplay/external/common_state.h"
#include "../../../middleware/gameplay/external/control_state_codec.h"
#include "../../../middleware/gameplay/peer/connect_messages.h"
#include "../../../middleware/gameplay/peer/established_packet.h"
#include "../../../middleware/gameplay/peer/reliable_assembly.h"
#include "../../bap/runtime.h"
#include "../gameplay_log.h"
#include "../group/group_host.h"
#include "peer_transport_internal.h"

namespace sunrise::server::gameplay::peer {

namespace {

namespace gp = state::gameplay;
namespace wire = middleware::gameplay::peer;
namespace bits = middleware::encoding::bits;

/** Delay sentinel used until a round trip has been measured. */
constexpr std::uint16_t kDelaySentinel = 1023;
/** Smallest head-minus-cursor the peer accepts. This host keeps at most one packet in flight. */
constexpr std::uint8_t kMinimumHeadCursor = 1;
/** One packet cannot report more delivered messages than this. */
constexpr std::size_t kMessageReportCapacity = 8;
/**
 * Milliseconds between two resends of the same queue. The peer discards a packet more than 128
 * sequences ahead of its window, so this host must not send faster than the peer does.
 */
constexpr std::uint64_t kResendInterval = 250;
/** Reflected root 0x80806AE6 after its lane-presence bit. */
constexpr std::size_t kPlayerSnapshotBits = 1373;

/**
 * Records one received packet sequence in the acknowledgement history.
 * @param peer Peer receiving the packet.
 * @param sequence Sequence the packet published.
 */
void record_sequence(gp::PeerLink& peer, std::uint16_t sequence) noexcept {
    if (!peer.ringInitialized) {
        peer.ringInitialized = true;
        peer.receiveHead = sequence;
        peer.received = {};
        return;
    }
    // Add the modulus before subtracting. A bare difference is signed and goes negative on a wrap.
    const std::uint16_t advance = static_cast<std::uint16_t>(
        (sequence + gp::kPacketSequenceModulus - peer.receiveHead) % gp::kPacketSequenceModulus);
    if (advance == 0 || advance >= gp::kPacketSequenceHalf) {
        // A repeat or an older packet leaves the published history alone.
        return;
    }
    std::array<bool, gp::kAckHistory> shifted{};
    for (std::size_t index = 0; index < shifted.size(); ++index) {
        // Entry `index` is the packet `index + 1` before the new head, so the old head lands at
        // `advance - 1`. Anything newer than the old head and older than this packet was skipped.
        if (index + 1 < advance) {
            continue;
        }
        if (index + 1 == advance) {
            shifted[index] = true;
            continue;
        }
        const std::size_t source = index - advance;
        shifted[index] = source < peer.received.size() && peer.received[source];
    }
    peer.received = shifted;
    peer.receiveHead = sequence;
}

/**
 * Applies one reassembled reliable message.
 * @param peer Peer that sent it, held under the lock.
 * @param message Reassembled message and its inner header.
 */
void apply_message(gp::PeerLink& peer, const wire::AssembledMessage& message) noexcept {
    if (message.id == static_cast<std::uint8_t>(wire::ConnectId::establish)
        && peer.stage == gp::PeerStage::connecting) {
        // The reliable establish is what moves a connected peer past the out-of-band pair.
        peer.stage = gp::PeerStage::connected;
    }
}

/**
 * Clears the send queue once the peer acknowledges the packet that carried it.
 * @param peer Peer whose acknowledgement arrived, held under the lock.
 * @param ack Acknowledgement state the packet published.
 * @return True when this acknowledgement emptied the queue.
 */
bool apply_acknowledgement(gp::PeerLink& peer, const wire::AckState& ack) noexcept {
    if (!peer.outbound.awaitingAcknowledgement
        || !wire::acknowledgement_covers(ack, peer.outbound.sentInPacket)) {
        return false;
    }
    // The peer has the packet, so every fragment in it is delivered. The next sequence is kept
    // because message sequences continue across messages.
    for (gp::OutboundFragment& fragment : peer.outbound.fragments) {
        fragment = {};
    }
    peer.outbound.count = 0;
    peer.outbound.awaitingAcknowledgement = false;
    return true;
}

/** Common state retained from one complete external frame. */
struct ParsedExternal {
    middleware::gameplay::external::CommonState common{};
    middleware::gameplay::external::SimulationEventBatch lane0{};
    middleware::gameplay::external::ControlStateBatch lane1{};
    middleware::gameplay::external::EntityBatch entities{};
    bool commonPresent{};
};

/** Exact external component that refused a frame. */
enum class ExternalReadResult : std::uint8_t {
    accepted,
    prefix,
    common,
    lane0,
    lane1,
    lane2,
    lane3,
    filler,
};

using middleware::gameplay::external::read_flag;

/** Reads or skips the receive-only player lane without retaining its local snapshot. */
[[nodiscard]] bool read_player_lane(bits::Reader& reader) noexcept {
    bool present = false;
    bool trailingList = false;
    return read_flag(reader, present)
           && (!present || (reader.skip(kPlayerSnapshotBits) && read_flag(reader, trailingList)));
}

/** Reads common, channels 0 to 3, and filler. */
[[nodiscard]] ExternalReadResult
read_external(std::span<const std::byte> payload,
              std::size_t bitOffset,
              std::uint64_t groupSessionId,
              const middleware::gameplay::external::Lane0Codec& lane0,
              const Lane0Transport& lane0Transport,
              const middleware::gameplay::external::TypePayloadCodec& entities,
              ParsedExternal& output) noexcept {
    bits::Reader reader(payload);
    ParsedExternal candidate{};
    bool lanePresent = false;
    bool externalPresent = false;
    if (!reader.skip(bitOffset) || !read_flag(reader, externalPresent) || !externalPresent) {
        return ExternalReadResult::prefix;
    }
    if (!read_flag(reader, candidate.commonPresent)
        || (candidate.commonPresent
            && !middleware::gameplay::external::read_common_state(reader, candidate.common))) {
        return ExternalReadResult::common;
    }
    if (lane0Transport.write != nullptr) {
        if (!middleware::gameplay::external::read_simulation_event_lane(
                reader, lane0Transport.payloadCodec, candidate.lane0)) {
            return ExternalReadResult::lane0;
        }
    } else if (lane0.read != nullptr) {
        if (!lane0.read(lane0.context, reader)) {
            return ExternalReadResult::lane0;
        }
    } else if (!read_flag(reader, lanePresent) || lanePresent) {
        return ExternalReadResult::lane0;
    }
    if (!middleware::gameplay::external::read_control_state_lane(
            reader,
            middleware::gameplay::external::control_state_payload_codec(),
            candidate.lane1)) {
        return ExternalReadResult::lane1;
    }
    if (g_entityTransport.read != nullptr
            ? !g_entityTransport.read(
                  g_entityTransport.context, groupSessionId, reader, candidate.entities)
            : !middleware::gameplay::external::read_entity_batch(
                  reader, entities, candidate.entities)) {
        return ExternalReadResult::lane2;
    }
    if (!read_player_lane(reader)) {
        return ExternalReadResult::lane3;
    }
    wire::FillerTrailer filler{};
    if (!wire::read_filler_and_padding(reader, filler) || reader.remaining_bits() != 0) {
        return ExternalReadResult::filler;
    }
    output = candidate;
    return ExternalReadResult::accepted;
}

/** @return Stable log name for one external read result. */
[[nodiscard]] const char* external_result_name(ExternalReadResult result) noexcept {
    constexpr std::array<const char*, 8> names = {
        "accepted", "prefix", "common", "lane0", "lane1", "lane2", "lane3", "filler"};
    const auto index = static_cast<std::size_t>(result);
    return index < names.size() ? names[index] : "unknown";
}

/** Queues message 44 after the peer transaction accepts the initial common root. */
void queue_common_request(const state::gameplay::Endpoint& from,
                          std::uint64_t groupSessionId,
                          const state::activity::SessionBinding& binding,
                          std::uint64_t ownerGeneration,
                          std::uint8_t requested) noexcept {
    if (!sunrise::server::bap::request_replication_epoch(binding, ownerGeneration, requested)) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    gp::PeerLink* const peer = find_locked(from);
    if (peer != nullptr && peer->externalGroupSessionId == groupSessionId
        && peer->commonReconciler.owner_generation() == ownerGeneration) {
        static_cast<void>(peer->commonReconciler.commit_request());
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/** Writes the external handler body after both reliable queues; true when all of it fit. */
[[nodiscard]] bool write_external(const gp::PeerLink& peer, bits::Writer& writer) noexcept {
    middleware::gameplay::external::CommonState common{};
    const auto viewPhase = peer.viewReceptor.phase();
    if (peer.stage != gp::PeerStage::connected
        || (viewPhase != gp::external::view_receptor::Phase::provisional
            && viewPhase != gp::external::view_receptor::Phase::accepted)
        || peer.externalGroupSessionId == 0 || !peer.commonReconciler.outbound_common(common)
        || (g_lane0Transport.write == nullptr && g_lane0Codec.write == nullptr)) {
        return false;
    }
    const bool commonPresent = !peer.commonCommitted;
    if (!writer.write(commonPresent ? 1U : 0U, 1)
        || (commonPresent && !middleware::gameplay::external::write_common_state(writer, common))
        || (g_lane0Transport.write != nullptr
                ? !g_lane0Transport.write(g_lane0Transport.context,
                                          peer.externalGroupSessionId,
                                          peer.nextExternalTransmission,
                                          writer)
                : !g_lane0Codec.write(g_lane0Codec.context, writer))
        || !writer.write(0, 1) || !writer.write(0, 1) || !writer.write(0, 1) || !writer.write(1, 1)
        || !writer.write(0, 1)) {
        return false;
    }
    return true;
}

/** Builds and sends one ACK packet from a peer copy taken under the lock. */
[[nodiscard]] bool send_acknowledgement(const gp::PeerLink& peer) noexcept {
    wire::AckState ack{};
    ack.outboundHead = peer.outboundHead;
    ack.outboundHeadPresent = peer.outboundHeadPresent;
    // The peer subtracts this from the decoded sequence to place its receive window. A zero
    // collapses that window and the peer discards every packet.
    ack.headMinusCursor = kMinimumHeadCursor;
    ack.receiveHead = peer.receiveHead;
    ack.ringInitialized = peer.ringInitialized;
    ack.received = peer.received;
    // No round trip is timed, so the delay field carries its sentinel.
    ack.delay = kDelaySentinel;

    std::array<std::byte, kReplyCapacity> buffer{};
    bits::Writer writer(buffer);
    const std::uint8_t guard = wire::connection_sequence_low2(peer.localConnectionSequence);
    std::size_t size = 0;
    // Only the 32-byte queue carries this host's messages; the 6-byte queue stays empty.
    middleware::gameplay::external::CommonState externalCommon{};
    const auto viewPhase = peer.viewReceptor.phase();
    const bool external = (viewPhase == gp::external::view_receptor::Phase::provisional
                           || viewPhase == gp::external::view_receptor::Phase::accepted)
                          && peer.externalGroupSessionId != 0
                          && peer.commonReconciler.outbound_common(externalCommon);
    if (!wire::write_head_and_ack(writer, guard, ack) || !wire::write_queue(writer, peer.outbound)
        || !wire::write_empty_queue(writer)) {
        return false;
    }
    if (external) {
        if (!writer.write(1, 1) || !write_external(peer, writer) || !writer.write(0, 1)) {
            return false;
        }
    } else if (!wire::write_absent_filler(writer)) {
        return false;
    }
    if (!writer.finish(size)) {
        return false;
    }
    return send_transport(peer.endpoint, {buffer.data(), size});
}

} // namespace

/** Consumes one established packet. */
void consume_established(const gp::Endpoint& from,
                         std::span<const std::byte> payload,
                         std::uint64_t now) noexcept {
    wire::EstablishedPacket packet{};
    if (!wire::decode_established(payload, true, packet)) {
        report(core::log::Level::debug, "ev=gameplay stage=packet result=drop reason=grammar");
        return;
    }
    std::array<std::uint8_t, kMessageReportCapacity> delivered{};
    std::size_t deliveredCount = 0;
    unsigned stage = 0;
    bool queueCleared = false;
    std::uint16_t clearedPacket = 0;
    std::uint64_t externalGroupSessionId = 0;
    // The reliable window never resynchronises, so a stalled queue is only visible as a refused
    // record against the sequence it is still waiting for.
    std::size_t largeDropped = 0;
    std::uint16_t largeNext = 0;
    std::uint16_t largeFirst = 0;
    bool peerFound = false;
    bool guardAccepted = false;
    bool externalExpected = false;
    bool externalValid = true;
    const char* externalFailure = "none";
    ParsedExternal external{};
    state::activity::SessionBinding commonBinding{};
    std::uint64_t commonOwnerGeneration = 0;
    std::uint8_t commonRequestedGeneration = 0;
    bool commonRequest = false;
    gp::external::common_reconciler::Reconciler commonCandidate{};
    bool commonCandidatePresent = false;
    DisplacedExternals completed{};
    std::size_t completedCount = 0;
    std::uint8_t expectedGuard = 0;
    AcquireSRWLockExclusive(&g_lock);
    gp::PeerLink* peer = find_locked(from);
    std::array<wire::AssembledMessage, kMessageReportCapacity> bodies{};
    if (peer != nullptr) {
        peerFound = true;
        expectedGuard = wire::connection_sequence_low2(peer->remoteConnectionSequence);
        guardAccepted = packet.connectionSequenceLow2 == expectedGuard;
        const auto phase = peer->viewReceptor.phase();
        externalExpected = phase == gp::external::view_receptor::Phase::provisional
                           || phase == gp::external::view_receptor::Phase::accepted;
        if (guardAccepted && externalExpected) {
            externalGroupSessionId = peer->externalGroupSessionId;
            const ExternalReadResult externalRead = read_external(payload,
                                                                  packet.externalBitOffset,
                                                                  externalGroupSessionId,
                                                                  g_lane0Codec,
                                                                  g_lane0Transport,
                                                                  g_entityCodec,
                                                                  external);
            externalValid = externalRead == ExternalReadResult::accepted;
            externalFailure = external_result_name(externalRead);
            if (externalValid && external.commonPresent) {
                commonCandidate = peer->commonReconciler;
                const auto result = commonCandidate.observe(external.common);
                externalValid =
                    result == gp::external::common_reconciler::ObserveResult::initialAccepted
                    || result
                           == gp::external::common_reconciler::ObserveResult::
                               awaitingRequestedGeneration
                    || result == gp::external::common_reconciler::ObserveResult::ready;
                if (!externalValid) {
                    externalFailure = "reconcile";
                }
                commonRequest =
                    result == gp::external::common_reconciler::ObserveResult::initialAccepted
                    && commonCandidate.pending_request(commonRequestedGeneration);
                if (commonRequest) {
                    commonBinding = peer->activityBinding;
                    commonOwnerGeneration = commonCandidate.owner_generation();
                }
                commonCandidatePresent = externalValid;
            }
            if (externalValid && externalGroupSessionId != 0 && external.entities.recordPresent) {
                externalValid = g_entityTransport.accepted != nullptr
                                    ? g_entityTransport.accepted(g_entityTransport.context,
                                                                 externalGroupSessionId,
                                                                 external.entities)
                                    : g_entityAccepted == nullptr
                                          || g_entityAccepted(g_entityAcceptedContext,
                                                              externalGroupSessionId,
                                                              external.entities);
                if (!externalValid) {
                    externalFailure = "lane2_accept";
                }
            }
            if (externalValid && externalGroupSessionId != 0
                && g_lane0Transport.accepted != nullptr) {
                externalValid = g_lane0Transport.accepted(
                    g_lane0Transport.context, externalGroupSessionId, external.lane0);
                if (!externalValid) {
                    externalFailure = "lane0_accept";
                }
            }
            if (externalValid && commonCandidatePresent) {
                peer->commonReconciler = commonCandidate;
            }
        }
    }
    if (guardAccepted) {
        // Reliable queues drain even when a later external component rejects its body.
        // ACKs belong to the validated transport header, independently of this
        // packet's incoming gameplay lanes. An unsupported lane must not pin
        // every outgoing contribution: after 128 packets that prevents sending
        // ACKs at all, leaving native deletion recipients permanently pending.
        completedCount = acknowledge_external(*peer, packet.ack, completed);
        if (packet.ack.outboundHeadPresent) {
            record_sequence(*peer, packet.ack.outboundHead);
        }
        clearedPacket = peer->outbound.sentInPacket;
        queueCleared = apply_acknowledgement(*peer, packet.ack);
        peer->acknowledgementOwed = true;
        peer->lastTick = now;
        largeDropped = wire::accept_records(packet.large, peer->large);
        largeNext = peer->large.nextSequence;
        largeFirst = packet.large.count == 0 ? 0 : packet.large.records[0].sequence;
        wire::accept_records(packet.small, peer->small);
        wire::AssembledMessage message{};
        while (wire::drain_message(peer->large, message)) {
            apply_message(*peer, message);
            if (deliveredCount < delivered.size()) {
                delivered[deliveredCount] = message.id;
                bodies[deliveredCount] = message;
                ++deliveredCount;
            }
        }
        while (wire::drain_message(peer->small, message)) {
            apply_message(*peer, message);
            if (deliveredCount < delivered.size()) {
                delivered[deliveredCount] = message.id;
                bodies[deliveredCount] = message;
                ++deliveredCount;
            }
        }
        stage = static_cast<unsigned>(peer->stage);
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!peerFound) {
        return;
    }
    if (!guardAccepted) {
        report(core::log::Level::debug,
               "ev=gameplay stage=packet result=drop reason=channel_low2 got=%u expect=%u",
               static_cast<unsigned>(packet.connectionSequenceLow2),
               static_cast<unsigned>(expectedGuard));
        return;
    }
    if (!externalValid) {
        report(core::log::Level::debug,
               "ev=gameplay stage=external result=drop reason=%s group=0x%016llX",
               externalFailure,
               static_cast<unsigned long long>(externalGroupSessionId));
    }
    if (externalValid && commonRequest && externalGroupSessionId != 0) {
        queue_common_request(from,
                             externalGroupSessionId,
                             commonBinding,
                             commonOwnerGeneration,
                             commonRequestedGeneration);
    }
    notify_external_outcomes(completed, completedCount);
    for (std::size_t index = 0; index < deliveredCount; ++index) {
        report(core::log::Level::info,
               "ev=gameplay stage=message result=ok id=%u peerstage=%u",
               static_cast<unsigned>(delivered[index]),
               stage);
        // The connect establish belongs to this layer and apply_message already took it, so
        // handing it to the group layer would only report it as undecoded on every connection.
        const wire::AssembledMessage& body = bodies[index];
        if (body.id == static_cast<std::uint8_t>(wire::ConnectId::establish)) {
            continue;
        }
        // Group handling runs outside the lock because answering takes it again.
        bits::Reader reader({body.bytes.data(), gp::kReassemblyCapacity});
        if (reader.skip(body.bodyBitOffset) && !group::consume(from, body.id, reader, now)) {
            report(core::log::Level::debug,
                   "ev=gameplay stage=message result=undecoded id=%u",
                   static_cast<unsigned>(body.id));
        }
    }
    if (queueCleared) {
        report(core::log::Level::info,
               "ev=gameplay stage=sendqueue result=cleared packet=%u base=%u entries=%u",
               static_cast<unsigned>(clearedPacket),
               static_cast<unsigned>(packet.ack.receiveHead),
               static_cast<unsigned>(packet.ack.reportedCount));
    }
    report(core::log::Level::debug,
           "ev=gameplay stage=packet result=ok seq=%u base=%u entries=%u large=%u small=%u "
           "first=%u next=%u drop=%zu",
           static_cast<unsigned>(packet.ack.outboundHead),
           static_cast<unsigned>(packet.ack.receiveHead),
           static_cast<unsigned>(packet.ack.reportedCount),
           static_cast<unsigned>(packet.large.count),
           static_cast<unsigned>(packet.small.count),
           static_cast<unsigned>(largeFirst),
           static_cast<unsigned>(largeNext),
           largeDropped);
}

/** Sends any owed acknowledgement. */
void service(std::uint64_t now) noexcept {
    std::array<gp::PeerLink, gp::kAssociationCapacity> owed{};
    std::size_t count = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (gp::PeerLink& peer : g_peers) {
        // An unacknowledged send queue keeps the packet going out until the peer confirms it.
        // Every packet burns one sequence, so the resend is paced.
        const bool resendDue = peer.outbound.count != 0 && now - peer.lastSend >= kResendInterval;
        const bool due = peer.acknowledgementOwed || resendDue;
        if (peer.stage == gp::PeerStage::absent || !due) {
            continue;
        }
        middleware::gameplay::external::CommonState externalCommon{};
        const auto viewPhase = peer.viewReceptor.phase();
        const bool external = (viewPhase == gp::external::view_receptor::Phase::provisional
                               || viewPhase == gp::external::view_receptor::Phase::accepted)
                              && peer.externalGroupSessionId != 0
                              && peer.commonReconciler.outbound_common(externalCommon);
        const std::uint16_t nextPacket =
            static_cast<std::uint16_t>((peer.outboundHead + 1) % gp::kPacketSequenceModulus);
        if (external
            && peer.externalContributions[nextPacket % peer.externalContributions.size()]
                   .occupied) {
            continue;
        }
        peer.acknowledgementOwed = false;
        peer.lastSend = now;
        // Only the first send of the current contents is stamped. A resend carries the same
        // fragments, so re-stamping would move the target past what the peer can acknowledge.
        if (peer.outbound.count != 0 && !peer.outbound.awaitingAcknowledgement) {
            peer.outbound.sentInPacket =
                static_cast<std::uint16_t>((peer.outboundHead + 1) % gp::kPacketSequenceModulus);
            peer.outbound.awaitingAcknowledgement = true;
        }
        // The packet sequence advances here so the copy carries the value it will publish.
        peer.outboundHead = nextPacket;
        peer.outboundHeadPresent = true;
        if (external) {
            ++peer.nextExternalTransmission;
            if (peer.nextExternalTransmission == 0) {
                ++peer.nextExternalTransmission;
            }
            auto& contribution =
                peer.externalContributions[nextPacket % peer.externalContributions.size()];
            contribution.transmissionId = peer.nextExternalTransmission;
            contribution.groupSessionId = peer.externalGroupSessionId;
            contribution.viewGeneration = peer.viewGeneration;
            contribution.packetSequence = nextPacket;
            contribution.commonPresent = !peer.commonCommitted;
            contribution.lane0Present = true;
            contribution.occupied = true;
        }
        peer.lastTick = now;
        owed[count] = peer;
        ++count;
    }
    ReleaseSRWLockExclusive(&g_lock);
    for (std::size_t index = 0; index < count; ++index) {
        if (send_acknowledgement(owed[index])) {
            continue;
        }
        report(core::log::Level::debug, "ev=gameplay stage=ack result=fail");
        DisplacedExternals displaced{};
        std::size_t displacedCount = 0;
        AcquireSRWLockExclusive(&g_lock);
        gp::PeerLink* const peer = find_locked(owed[index].endpoint);
        if (peer != nullptr && peer->peerGeneration == owed[index].peerGeneration) {
            peer->acknowledgementOwed = true;
            if (peer->outbound.awaitingAcknowledgement
                && peer->outbound.sentInPacket == owed[index].outboundHead) {
                peer->outbound.awaitingAcknowledgement = false;
            }
            auto& reserved = peer->externalContributions[owed[index].outboundHead
                                                         % peer->externalContributions.size()];
            // The packet never left, so the stake is displaced like any other lost contribution.
            if (reserved.occupied
                && reserved.transmissionId == owed[index].nextExternalTransmission) {
                displaced[displacedCount++] = {
                    reserved.groupSessionId, reserved.transmissionId, wire::AckOutcome::unresolved};
                reserved = {};
            }
        }
        ReleaseSRWLockExclusive(&g_lock);
        notify_external_outcomes(displaced, displacedCount);
    }
}

} // namespace sunrise::server::gameplay::peer
