#include "group_host.h"

#include <Windows.h>

#include <array>
#include <atomic>

#include "../../../core/settings/settings.h"
#include "../../../middleware/gameplay/descriptor/join_descriptor.h"
#include "../../../middleware/gameplay/group/member_messages.h"
#include "../../../middleware/gameplay/group/parameter_messages.h"
#include "../../../middleware/gameplay/group/parameter_registry.h"
#include "../../../middleware/gameplay/group/session_messages.h"
#include "../../../middleware/gameplay/group/session_state.h"
#include "../../../middleware/gameplay/group/view_message.h"
#include "../../../state/activity/runtime.h"
#include "../../bap/runtime.h"
#include "../endpoint/gameplay_endpoint.h"
#include "../gameplay_advertisement.h"
#include "../gameplay_log.h"
#include "../peer/peer_transport.h"
#include "group_host_internal.h"
#include "group_host_parameters.h"
#include "group_host_sessions.h"
#include "group_migration_receipts.h"

namespace sunrise::server::gameplay::group {

namespace {

namespace wire = middleware::gameplay::group;
namespace bits = middleware::encoding::bits;
namespace descriptor = middleware::gameplay::descriptor;

/** A membership snapshot is far larger than kBodyCapacity, and the reliable queue bounds it. */
constexpr std::size_t kMembershipBodyCapacity = 640;
/** Only the low registry bits of a request bitmap name a parameter. */
constexpr std::uint64_t kParameterMaskBits = (std::uint64_t{1} << wire::kParameterCount) - 1U;
/** Registry index the join-latch update names. Any index would do; none is ever filled. */
constexpr std::uint8_t kJoinLatchParameter = 0;
/** Peers this host tracks at once. The public POC admits one. */
constexpr std::size_t kAdmittedCapacity = 4;
/** Player slot the admitted peer's player takes. */
constexpr std::uint32_t kPeerPlayerSlot = 0;
/** Counter the first player of a session carries. The consumer's own add starts here too. */
constexpr std::uint32_t kFirstAddSequence = 0;

/** One admitted peer and the player it asked this host to add. */
struct Admitted {
    state::gameplay::Endpoint endpoint{};
    std::uint64_t joinId{};
    /** Machine id the peer's join request carried for itself. Zero when its row was not found. */
    std::uint64_t machineId{};
    std::uint64_t playerId{};
    /** Player kind and soid pair the peer's own player-add carried, republished verbatim. */
    std::uint8_t playerKind{};
    wire::PlayerBlockSoids playerSoids{};
    /** Group-session id the peer named in its join request, which its parameters must echo. */
    std::uint64_t sessionId{};
    bool occupied{};
    bool hasPlayer{};
    /** The peer has reported its join finished, so its member state is `established`. */
    bool joinComplete{};
    /** The reliable queue refused the `activity-host` parameter. Nothing asks for it again. */
    bool parameterOwed{};
    /** Order in which the peer last named this session. The lowest is the least recently used. */
    std::uint64_t lastUse{};
};

/**
 * Public group sessions the peer holds at once: one current and one target.
 * The peer resolves a session through a two-element array, so a third is one it left.
 */
constexpr std::size_t kPublicSessionCapacity = 2;

/** Revision of the last published snapshot. The consumer refuses one that does not increase. */
std::atomic<std::uint32_t> g_membershipRevision{0};
/** Stamps `Admitted::lastUse`. It only has to order the records, so it never has to be a clock. */
std::atomic<std::uint64_t> g_admitClock{0};
/** Guards the admitted table against the worker and the callback pump. */
SRWLOCK g_admittedLock{SRWLOCK_INIT};
/** Admitted peers. A join claims a slot and a leave never reclaims one in this POC. */
std::array<Admitted, kAdmittedCapacity> g_admitted{};

/** Member state this host publishes for every member carrying the join id. */
constexpr wire::MemberState kJoinMemberState = wire::MemberState::ready;

// The joining peer's entry must be at least `joined` and must not be `established`, and its request
// waits until every member carrying the join id reads `ready`.
static_assert(static_cast<std::uint8_t>(kJoinMemberState)
                  >= static_cast<std::uint8_t>(wire::MemberState::joined),
              "the published member state must clear the peer's own join bar");
static_assert(kJoinMemberState == wire::MemberState::ready,
              "the request advances only when every member carrying the join id reads ready");

/**
 * Every occupied record carries a nonzero session, so a zero key matches nothing.
 * @param sessionId Group session the table is keyed by.
 * @return The one record holding that session, or null. The caller holds the lock.
 */
[[nodiscard]] Admitted* find_admitted(std::uint64_t sessionId) noexcept {
    for (Admitted& entry : g_admitted) {
        if (entry.occupied && entry.sessionId == sessionId) {
            return &entry;
        }
    }
    return nullptr;
}

/**
 * Finds or claims the record for one peer, and binds it to that peer's endpoint.
 * A client that rebuilds its channel arrives from a new port and joins the same session again.
 * @param peer Peer endpoint.
 * @param sessionId Group session the record is keyed by. Zero claims nothing.
 * @return Record for that session, or null when the table is full.
 */
[[nodiscard]] Admitted* claim(const state::gameplay::Endpoint& peer,
                              std::uint64_t sessionId) noexcept {
    if (sessionId == 0) {
        return nullptr;
    }
    // Keyed by session, not endpoint: one client holds a record per public region and both records
    // name the same endpoint.
    Admitted* found = find_admitted(sessionId);
    if (found == nullptr) {
        for (Admitted& entry : g_admitted) {
            if (!entry.occupied) {
                entry.occupied = true;
                entry.sessionId = sessionId;
                found = &entry;
                break;
            }
        }
    }
    if (found != nullptr) {
        found->endpoint = peer;
        found->lastUse = g_admitClock.fetch_add(1) + 1;
    }
    return found;
}

/**
 * Finds the record for one session and proves the sender owns it.
 * Every later message names its own session, so without this a peer could move the state of a
 * session another endpoint was admitted for.
 * @param peer Peer endpoint the message arrived from.
 * @param sessionId Group session the message named.
 * @return Record for that session, or null when it is absent or owned by another endpoint.
 */
[[nodiscard]] Admitted* find_owned(const state::gameplay::Endpoint& peer,
                                   std::uint64_t sessionId) noexcept {
    Admitted* const found = find_admitted(sessionId);
    if (found == nullptr || found->endpoint != peer) {
        return nullptr;
    }
    found->lastUse = g_admitClock.fetch_add(1) + 1;
    return found;
}

/**
 * Tests whether another endpoint was admitted for one session.
 * An absent record is not a conflict: a message may name a session before this host has a record
 * for it, and refusing that would strand the peer.
 * @param peer Peer endpoint the message arrived from.
 * @param sessionId Group session the message named.
 * @return True when a record holds that session for a different endpoint.
 */
[[nodiscard]] bool owned_elsewhere(const state::gameplay::Endpoint& peer,
                                   std::uint64_t sessionId) noexcept {
    AcquireSRWLockShared(&g_admittedLock);
    const Admitted* const found = find_admitted(sessionId);
    const bool conflict = found != nullptr && found->endpoint != peer;
    ReleaseSRWLockShared(&g_admittedLock);
    return conflict;
}

/**
 * Counts the players one session holds, which is what its free join slots are measured against.
 * @param sessionId Group session the message named.
 * @return 1 when the admitted peer owns a player in that session, otherwise 0.
 */
[[nodiscard]] std::uint8_t session_player_count(std::uint64_t sessionId) noexcept {
    AcquireSRWLockShared(&g_admittedLock);
    const Admitted* const found = find_admitted(sessionId);
    const std::uint8_t count = found != nullptr && found->hasPlayer ? 1U : 0U;
    ReleaseSRWLockShared(&g_admittedLock);
    return count;
}

/**
 * Publishes one snapshot naming this host, one admitted peer, and that peer's player if it has
 * one. The caller holds the admitted lock.
 * @param record Admitted peer the snapshot names.
 * @return True when the snapshot was queued on the peer's reliable channel.
 */
[[nodiscard]] bool publish_snapshot(const Admitted& record) noexcept {
    const state::gameplay::Endpoint host = endpoint::advertised();
    std::array<wire::MembershipMember, kSnapshotMemberCount> members{};
    // The port this peer dialled, not the primary one. Each host row advertises its own, and a
    // snapshot naming a different port names a host this peer never joined.
    const std::uint16_t hostPort =
        record.endpoint.localPort != 0 ? record.endpoint.localPort : host.port;
    descriptor::write_net_addr(host.address, hostPort, members[kHostMemberIndex].address);
    // The session id is the machine id this region's descriptor advertised, and the client joined
    // through it. The whole-process identity would name a host this session never saw.
    members[kHostMemberIndex].machineId = record.sessionId;
    // The consumer refuses a table with no entry it recognises as itself, so the peer's own blob is
    // echoed. A blob rebuilt from the endpoint it arrived from is not the same bytes.
    if (!peer::remote_address(record.sessionId, members[kPeerMemberIndex].address)) {
        descriptor::write_net_addr(
            record.endpoint.address, record.endpoint.port, members[kPeerMemberIndex].address);
    }
    // The peer's own row names its machine id. The join id stands in only when no row matched
    // this link's address, so the entry still resolves.
    members[kPeerMemberIndex].machineId = record.machineId != 0 ? record.machineId : record.joinId;
    members[kPeerMemberIndex].joinId = record.joinId;
    // The peer ends its join request once no session holds more than one member with that id, so
    // both entries carry it. A table naming it once says the join is over.
    members[kHostMemberIndex].joinId = record.joinId;
    for (wire::MembershipMember& member : members) {
        // The connection group is what makes the consumer resolve the member's peer link. This
        // host has no value for join compatibility or the join timestamp, so both stay cleared.
        member.connectionPresent = true;
    }
    // Both entries carry the join id, so both take the same state. Once the peer reports its join
    // finished they move to `established`, which is what stops it re-sending that report.
    const wire::MemberState state =
        record.joinComplete ? wire::MemberState::established : kJoinMemberState;
    members[kHostMemberIndex].state = state;
    members[kPeerMemberIndex].state = state;

    std::array<wire::MembershipPlayer, 1> players{};
    players[0].slot = kPeerPlayerSlot;
    players[0].playerId = record.playerId;
    players[0].memberIndex = kPeerMemberIndex;
    players[0].addSequence = kFirstAddSequence;
    // The peer reads its own account and character back out of this row to build every activity
    // join request. A row without them makes it send zeros and refuse to create its own player.
    players[0].hasProfile = record.playerSoids.present;
    players[0].profileKind = record.playerKind;
    players[0].accountSoid = record.playerSoids.accountSoid;
    players[0].characterSoid = record.playerSoids.characterSoid;
    if (record.hasPlayer) {
        members[kPeerMemberIndex].ownsPlayerSlot = true;
        members[kPeerMemberIndex].playerSlot = kPeerPlayerSlot;
    }

    wire::MembershipUpdate update{};
    // The same per-region machine id the member table carries.
    update.hostMachineId = record.sessionId;
    update.revision = g_membershipRevision.fetch_add(1) + 1;
    update.hostMemberIndex = kHostMemberIndex;
    update.successionIndex = kHostMemberIndex;
    update.members = members;
    if (record.hasPlayer) {
        update.players = players;
    }

    std::array<std::byte, kMembershipBodyCapacity> body{};
    bits::Writer writer(body);
    std::size_t size = 0;
    if (!wire::write_membership_update(writer, update) || !writer.finish(size)) {
        return false;
    }
    // The peer logs the hash it wanted, so ours has to be logged next to it to read a mismatch.
    report(core::log::Level::info,
           "ev=gameplay stage=membership result=built revision=%u members=%zu players=%zu "
           "profile=%u bytes=%zu hash=0x%08X",
           update.revision,
           update.members.size(),
           update.players.size(),
           update.players.empty() || !players[0].hasProfile ? 0U : 1U,
           size,
           wire::session_state_hash(update));
    return peer::enqueue_reliable(
        record.sessionId,
        static_cast<std::uint8_t>(wire::SessionMessageId::membershipUpdate),
        wire::kMembershipUpdateSize,
        {body.data(), size},
        writer.bit_count());
}

/**
 * Advances one view-establishment stage and commits only an enqueued response.
 * The binding is keyed by the link because the body names no session.
 * @param from Peer endpoint the view arrived from.
 * @param view Decoded view body.
 */
void bind_view(const state::gameplay::Endpoint& from,
               const wire::ViewEstablishment& view) noexcept {
    server::bap::ActivityReplicationView activity{};
    if (server::bap::activity_replication_view_for_session(view.sessionToken, activity)) {
        std::uint64_t groupSessionId = activity.groupSessionId;
        HostSessionBinding privateHost{};
        if (groupSessionId == 0
            && server::gameplay::private_host_session(activity.binding, privateHost)) {
            groupSessionId = privateHost.groupSessionId;
        }
        static_cast<void>(peer::open_external_common(from,
                                                     groupSessionId,
                                                     activity.binding,
                                                     activity.patchEpoch,
                                                     activity.activityClientGeneration,
                                                     activity.replicationEpoch));
    }
    wire::ViewEstablishment response{};
    std::uint64_t generation = 0;
    const peer::ViewStageResult result = peer::receive_view_stage(from, view, response, generation);
    const bool sent = result == peer::ViewStageResult::accepted
                      && send_reliable(from,
                                       wire::kViewMessageId,
                                       wire::kViewMessageSize,
                                       [&response](bits::Writer& writer) noexcept {
                                           return wire::write_view(writer, response);
                                       });
    const bool committed = sent && peer::commit_view_response(from, generation);
    report(sent ? core::log::Level::info : core::log::Level::warn,
           "ev=gameplay stage=view result=%s remote=%u local=%u token=0x%llX list=%u",
           committed                                   ? "queued"
           : result == peer::ViewStageResult::accepted ? "queue_fail"
                                                       : "refused",
           static_cast<unsigned>(view.kind),
           static_cast<unsigned>(response.kind),
           static_cast<unsigned long long>(view.sessionToken),
           static_cast<unsigned>(view.listCount));
}

/**
 * Answers one time-synchronize request with the three-sample reply.
 * A reply is terminal and gets no answer.
 * @param from Peer endpoint.
 * @param probe Decoded body.
 * @param now Tick the request arrived on.
 */
void answer_time(const state::gameplay::Endpoint& from,
                 const wire::TimeSynchronize& probe,
                 std::uint64_t now) noexcept {
    if (probe.threeSample) {
        return;
    }
    wire::TimeSynchronize reply{};
    reply.sessionId = probe.sessionId;
    reply.threeSample = true;
    reply.requesterSendTime = probe.requesterSendTime;
    reply.responderReceiveTime = now;
    // The peer derives its clock offset from all four stamps, so the send stamp is taken last.
    reply.responderSendTime = GetTickCount64();
    if (!peer::send_out_of_band(from,
                                static_cast<std::uint8_t>(wire::SessionMessageId::timeSynchronize),
                                wire::kTimeSynchronizeSize,
                                [&reply](bits::Writer& writer) noexcept {
                                    return wire::write_time_synchronize(writer, reply);
                                })) {
        report(core::log::Level::debug, "ev=gameplay stage=time result=fail");
    }
}

/**
 * Drops one session's link and its admitted record together.
 * A leave names one region's session, and the client's other region must keep its own link.
 * @param sessionId Session the peer is leaving.
 */
void drop_session(std::uint64_t sessionId) noexcept {
    peer::drop(sessionId);
    // The region's activity host stays. A leave is also how the peer fast travels to the region it
    // is already in, and a fresh id there is `public_activity_host_mismatch`.
    AcquireSRWLockExclusive(&g_admittedLock);
    if (Admitted* const record = find_admitted(sessionId); record != nullptr) {
        *record = {};
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
}

/**
 * Acknowledges one leave and drops the session it names.
 * @return True when the body was decoded and the container may continue.
 */
[[nodiscard]] bool consume_leave(const state::gameplay::Endpoint& from,
                                 bits::Reader& reader) noexcept {
    std::uint64_t leaving = 0;
    if (!wire::read_session_only(reader, leaving)) {
        return false;
    }
    // A leave tears the session's link down, so a peer must not be able to send one for a session
    // another endpoint was admitted for.
    if (owned_elsewhere(from, leaving)) {
        report(core::log::Level::warn,
               "ev=gameplay stage=leave result=unowned session=0x%016llX",
               static_cast<unsigned long long>(leaving));
        return true;
    }
    const bool sent =
        peer::send_out_of_band(from,
                               static_cast<std::uint8_t>(wire::SessionMessageId::leaveAcknowledge),
                               wire::kLeaveAcknowledgeSize,
                               [leaving](bits::Writer& writer) noexcept {
                                   return wire::write_session_only(writer, leaving);
                               });
    report(core::log::Level::info,
           "ev=gameplay stage=leave result=%s session=0x%016llX",
           sent ? "acknowledged" : "fail",
           static_cast<unsigned long long>(leaving));
    // A failed send keeps the state the peer's repeated leave needs.
    if (sent) {
        drop_session(leaving);
    }
    return true;
}

/**
 * Answers one join-completion report with the parameter it still owes and a fresh snapshot.
 * The peer repeats this until its membership shows every member of the join at `established`.
 * @return True when the body was decoded and the container may continue.
 */
[[nodiscard]] bool consume_join_complete(const state::gameplay::Endpoint& from,
                                         bits::Reader& reader) noexcept {
    wire::JoinComplete body{};
    if (!wire::read_join_complete(reader, body)) {
        return false;
    }
    // Keyed by the body's session, not the link's: one link carries every region the client joined
    // over it.
    AcquireSRWLockExclusive(&g_admittedLock);
    Admitted* const record = find_owned(from, body.sessionId);
    bool queued = false;
    if (record != nullptr) {
        // The state change the report implies. The parameter follows that change, not the report,
        // so a repeat does not publish it twice.
        const bool joined = !record->joinComplete;
        record->joinComplete = true;
        // Before the snapshot, because it is the smaller message. A queue that then refuses the
        // snapshot is answered by the peer's next repeat of this same report.
        if (joined) {
            record->parameterOwed = !publish_activity_host(record->sessionId);
        }
        queued = publish_snapshot(*record);
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
    report(queued ? core::log::Level::info : core::log::Level::debug,
           "ev=gameplay stage=join result=%s session=0x%llX machine=0x%llX update=%u",
           queued              ? "completed"
           : record == nullptr ? "fail"
                               : "deferred",
           static_cast<unsigned long long>(body.sessionId),
           static_cast<unsigned long long>(body.machineId),
           body.joinSequence);
    return true;
}

/**
 * Drops the session one abandoned join names.
 * @return True when the body was decoded and the container may continue.
 */
[[nodiscard]] bool consume_join_abort(const state::gameplay::Endpoint& from,
                                      bits::Reader& reader) noexcept {
    wire::SessionNotice notice{};
    if (!wire::read_join_abort(reader, notice)) {
        return false;
    }
    if (owned_elsewhere(from, notice.sessionId)) {
        report(core::log::Level::warn,
               "ev=gameplay stage=join result=unowned_abort session=0x%016llX",
               static_cast<unsigned long long>(notice.sessionId));
        return true;
    }
    report(core::log::Level::info,
           "ev=gameplay stage=join result=abort session=0x%016llX",
           static_cast<unsigned long long>(notice.sessionId));
    drop_session(notice.sessionId);
    return true;
}

/**
 * Answers one parameter request with every requested parameter this host can encode.
 * @return True only when every selected body was located, which is what leaves the container
 *         readable behind the request.
 */
[[nodiscard]] bool consume_parameter_request(const state::gameplay::Endpoint& from,
                                             bits::Reader& reader) noexcept {
    wire::ParameterRequestHeader header{};
    if (!wire::read_parameter_request(reader, header)) {
        return false;
    }
    const std::uint64_t mask = header.requestedMask & kParameterMaskBits;
    std::array<char, kParameterNameCapacity> names{};
    report(core::log::Level::info,
           "ev=gameplay stage=parameters result=request mask=0x%08X mode=%u names=%s",
           static_cast<unsigned>(mask),
           static_cast<unsigned>(header.modeFlag ? 1U : 0U),
           wire::parameter_names(mask, names.data(), names.size()));
    // The selected bodies are walked before the answer goes out, so nothing is answered from a
    // request that was only read as far as its header.
    wire::ParameterRequestWalk walk{};
    const bool intact = wire::walk_parameter_request(reader, mask, walk);
    report(walk.complete ? core::log::Level::debug : core::log::Level::info,
           "ev=gameplay stage=parameters result=%s walked=0x%08X stopped=%u tail=%u",
           walk.complete ? "framed"
           : intact      ? "ambiguous"
                         : "truncated",
           static_cast<unsigned>(walk.walkedMask),
           static_cast<unsigned>(walk.ambiguousParameter),
           walk.tailBits);
    // The peer builds no activity client until it holds the host parameter, so the answer goes out
    // even when a later body could not be located. A session another endpoint holds is answered by
    // that endpoint.
    if (!owned_elsewhere(from, header.sessionId)) {
        answer_parameters(header.sessionId, mask, session_player_count(header.sessionId));
    }
    return walk.complete;
}

/**
 * Adopts the player one peer asked this host to add and republishes the snapshot.
 * @return False always: the player block and its tail are not decoded, so the container ends here.
 */
[[nodiscard]] bool consume_player_add(const state::gameplay::Endpoint& from,
                                      bits::Reader& reader) noexcept {
    wire::PlayerAddRequest request{};
    if (!wire::read_player_add(reader, request)) {
        return false;
    }
    // The published row carries the identity group only. The profile block behind it has no encoder
    // here, and the peer's clear-flag arm accepts a row without one.
    AcquireSRWLockExclusive(&g_admittedLock);
    // The body's session, for the same reason join-complete uses its own.
    Admitted* const record = find_owned(from, request.sessionId);
    bool published = false;
    if (record != nullptr) {
        record->hasPlayer = true;
        record->playerId = request.playerId;
        record->playerKind = request.kind;
        record->playerSoids = request.soids;
        published = publish_snapshot(*record);
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
    report(core::log::Level::info,
           "ev=gameplay stage=player result=%s session=0x%llX player=0x%llX seq=%u kind=%u "
           "acct=0x%llX character=0x%llX",
           published ? "added" : "fail",
           static_cast<unsigned long long>(request.sessionId),
           static_cast<unsigned long long>(request.playerId),
           request.sequence,
           static_cast<unsigned>(request.kind),
           static_cast<unsigned long long>(request.soids.accountSoid),
           static_cast<unsigned long long>(request.soids.characterSoid));
    return false;
}

/**
 * Drops the player the named session's record holds. The message names no player itself.
 * @return True when the body was decoded. The body is two fields, so the container continues.
 */
[[nodiscard]] bool consume_player_remove(const state::gameplay::Endpoint& from,
                                         bits::Reader& reader) noexcept {
    wire::PlayerRemoveRequest request{};
    if (!wire::read_player_remove(reader, request)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_admittedLock);
    Admitted* const record = find_owned(from, request.sessionId);
    bool published = false;
    if (record != nullptr && record->hasPlayer) {
        record->hasPlayer = false;
        record->playerId = 0;
        record->playerKind = 0;
        record->playerSoids = {};
        published = publish_snapshot(*record);
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
    report(core::log::Level::info,
           "ev=gameplay stage=player result=%s session=0x%llX",
           published           ? "removed"
           : record == nullptr ? "fail"
                               : "absent",
           static_cast<unsigned long long>(request.sessionId));
    return true;
}

} // namespace

/** Frees every admitted record at one endpoint. */
void release_endpoint(const state::gameplay::Endpoint& endpoint) noexcept {
    std::size_t count = 0;
    AcquireSRWLockExclusive(&g_admittedLock);
    for (Admitted& entry : g_admitted) {
        if (entry.occupied && entry.endpoint == endpoint) {
            ++count;
            entry = {};
        }
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
    if (count != 0) {
        report(core::log::Level::info,
               "ev=gameplay stage=admitted result=dropped endpoint=0x%08X:%u sessions=%zu",
               endpoint.address,
               static_cast<unsigned>(endpoint.port),
               count);
    }
}

/** Consumes one group-session message. */
bool consume(const state::gameplay::Endpoint& from,
             std::uint8_t id,
             bits::Reader& reader,
             std::uint64_t now) noexcept {
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::timeSynchronize)) {
        wire::TimeSynchronize probe{};
        if (!wire::read_time_synchronize(reader, probe)) {
            return false;
        }
        answer_time(from, probe, now);
        return true;
    }
    if (id == wire::kViewMessageId) {
        wire::ViewEstablishment view{};
        if (!wire::read_view(reader, view)) {
            return false;
        }
        bind_view(from, view);
        return true;
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::leaveSession)) {
        return consume_leave(from, reader);
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::peerEstablish)) {
        std::uint64_t established = 0;
        if (!wire::read_session_only(reader, established)) {
            return false;
        }
        report(core::log::Level::info,
               "ev=gameplay stage=establish result=ok session=0x%016llX",
               static_cast<unsigned long long>(established));
        return true;
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::joinComplete)) {
        return consume_join_complete(from, reader);
    }
    if (id == static_cast<std::uint8_t>(wire::SessionMessageId::joinAbort)) {
        return consume_join_abort(from, reader);
    }
    if (id == wire::kParameterRequestId) {
        return consume_parameter_request(from, reader);
    }
    if (id == wire::kPeerPropertiesId) {
        wire::PeerPropertiesHeader header{};
        if (!wire::read_peer_properties_header(reader, header)) {
            return false;
        }
        // The 304-byte property block behind the address is not decoded, so the body is
        // reported and not consumed.
        report(core::log::Level::info,
               "ev=gameplay stage=properties result=read session=0x%llX method=%u",
               static_cast<unsigned long long>(header.sessionId),
               static_cast<unsigned>(header.addressMethod));
        return false;
    }
    if (id == wire::kPlayerAddId) {
        return consume_player_add(from, reader);
    }
    if (id == wire::kPlayerRemoveId) {
        return consume_player_remove(from, reader);
    }
    if (id == wire::kPlayerPropertiesId) {
        wire::PlayerPropertiesRequest request{};
        if (!wire::read_player_properties_header(reader, request)) {
            return false;
        }
        // The sparse record behind the header is not decoded, so nothing is merged from it. A
        // merge from the header alone would reset every field the record carries.
        report(core::log::Level::info,
               "ev=gameplay stage=player result=properties session=0x%llX seq=%u kind=%u",
               static_cast<unsigned long long>(request.sessionId),
               request.sequence,
               static_cast<unsigned>(request.kind));
        return false;
    }
    return migration::consume(from, id, reader);
}

/** Publishes the membership snapshot that completes one peer's join. */
bool publish_membership(const state::gameplay::Endpoint& peer,
                        std::uint64_t peerJoinId,
                        std::uint64_t peerMachineId,
                        std::uint64_t sessionId) noexcept {
    AcquireSRWLockExclusive(&g_admittedLock);
    Admitted* const record = claim(peer, sessionId);
    bool published = false;
    if (record != nullptr) {
        // A retried join brings a new join id and drops any player the previous attempt added.
        // It also starts again at `ready`, so the previous attempt's completion does not carry.
        record->joinId = peerJoinId;
        record->machineId = peerMachineId;
        record->sessionId = sessionId;
        record->hasPlayer = false;
        record->playerId = 0;
        record->playerKind = 0;
        record->playerSoids = {};
        record->joinComplete = false;
        record->parameterOwed = false;
        published = publish_snapshot(*record);
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
    return published;
}

/** Checks whether one endpoint owns an admitted session row. */
bool admitted_owner(const state::gameplay::Endpoint& endpoint, std::uint64_t sessionId) noexcept {
    AcquireSRWLockShared(&g_admittedLock);
    const Admitted* const admitted = find_admitted(sessionId);
    const bool owned = admitted != nullptr && admitted->endpoint == endpoint;
    ReleaseSRWLockShared(&g_admittedLock);
    return owned;
}

/** Sends the one publish that has no client message behind it, and retires a stale record. */
void service(std::uint64_t) noexcept {
    // Outside any staged push, so the state revision it advances cannot fail a transaction guard.
    allocate_claimed_host_sessions();
    // The peer drops a stale target locally and sends no leave for it. Such a record shows up
    // only as the least recently named one over the capacity.
    std::uint64_t retired = 0;
    std::array<std::uint64_t, kAdmittedCapacity> activeSessions{};
    std::size_t activeCount = 0;
    AcquireSRWLockExclusive(&g_admittedLock);
    std::size_t occupied = 0;
    Admitted* oldest = nullptr;
    for (Admitted& record : g_admitted) {
        if (!record.occupied) {
            continue;
        }
        ++occupied;
        if (oldest == nullptr || record.lastUse < oldest->lastUse) {
            oldest = &record;
        }
    }
    if (occupied > kPublicSessionCapacity && oldest != nullptr) {
        retired = oldest->sessionId;
        *oldest = {};
    }
    for (Admitted& record : g_admitted) {
        if (!record.occupied) {
            continue;
        }
        activeSessions[activeCount++] = record.sessionId;
        // Every snapshot answers a message the peer sends again while its membership still lacks
        // one, so the parameter is the only publish left to send from here.
        if (record.parameterOwed) {
            record.parameterOwed = !publish_activity_host(record.sessionId);
        }
    }
    ReleaseSRWLockExclusive(&g_admittedLock);
    for (std::size_t index = 0; index < activeCount; ++index) {
        server::bap::ActivityReplicationView view{};
        if (server::bap::activity_replication_view_for_group(activeSessions[index], view)) {
            static_cast<void>(peer::open_external_common(view.groupSessionId,
                                                         view.binding,
                                                         view.patchEpoch,
                                                         view.activityClientGeneration,
                                                         view.replicationEpoch));
        }
    }
    // Outside the lock, in the order drop_session already uses. The region's activity host stays:
    // the peer rotates back into a region it has not left, and a fresh id there is a hard error.
    if (retired != 0) {
        peer::drop(retired);
        report(core::log::Level::info,
               "ev=gameplay stage=admitted result=retired session=0x%016llX held=%zu",
               static_cast<unsigned long long>(retired),
               occupied - 1);
    }
}

/** Publishes the parameter update a joining peer needs before it will finish its join. */
bool publish_join_parameters(std::uint64_t sessionId) noexcept {
    // A joining peer finishes only once it has applied one update, whatever it names. Releasing a
    // slot the peer never filled sets that latch and leaves the peer's state alone.
    wire::ParameterUpdate update{};
    update.sessionId = sessionId;
    update.releasedMask = std::uint64_t{1} << kJoinLatchParameter;

    const bool sent = send_parameter_update(update);
    std::array<char, kParameterNameCapacity> names{};
    report(sent ? core::log::Level::info : core::log::Level::warn,
           "ev=gameplay stage=parameters result=%s released=0x%08X names=%s",
           sent ? "queued" : "fail",
           static_cast<unsigned>(update.releasedMask),
           wire::parameter_names(update.releasedMask, names.data(), names.size()));
    return sent;
}

/** Reports whether replication may produce entity output for one peer. */
bool view_accepted(std::uint64_t sessionId) noexcept {
    return peer::view_bound(sessionId);
}

/** Copies every admitted group-session record. */
void snapshot_admitted(std::span<AdmittedRow> output, std::size_t& count) noexcept {
    count = 0;
    AcquireSRWLockShared(&g_admittedLock);
    for (const Admitted& entry : g_admitted) {
        if (!entry.occupied || count >= output.size()) {
            continue;
        }
        output[count] = {entry.sessionId,
                         entry.endpoint,
                         entry.joinComplete,
                         entry.joinComplete && !entry.parameterOwed,
                         entry.hasPlayer,
                         entry.joinId};
        ++count;
    }
    ReleaseSRWLockShared(&g_admittedLock);
}

/** Clears every group-session record. */
void reset() noexcept {
    g_membershipRevision.store(0);
    // Every host session goes back to State as well, or its records are stranded there.
    reset_host_sessions();
    AcquireSRWLockExclusive(&g_admittedLock);
    g_admitted = {};
    ReleaseSRWLockExclusive(&g_admittedLock);
}

} // namespace sunrise::server::gameplay::group
