#include "association_host.h"

#include <Windows.h>

#include <array>

#include "../../../middleware/crypto/random_bytes.h"
#include "../../../middleware/gameplay/association/control_packet.h"
#include "../../../middleware/gameplay/association/protected_datagram.h"
#include "../../../middleware/gameplay/association/srp_exchange.h"
#include "../endpoint/gameplay_endpoint.h"
#include "../gameplay_log.h"
#include "../group/group_host.h"
#include "../peer/peer_transport.h"

namespace sunrise::server::gameplay::association {

namespace {

namespace wire = middleware::gameplay::association;
namespace srp = middleware::gameplay::association::srp;

/** Shortest datagram that can hold its association words and clear trailer. */
constexpr std::size_t kMinimumDatagram = sizeof(std::uint32_t) * 2U + 1U + wire::kTrailerSize;

SRWLOCK g_lock{SRWLOCK_INIT};
state::gameplay::GameplayState g_state;

/** Clears one association and wipes its key material. Callers already hold the lock. */
void drop_locked(state::gameplay::Association& association) noexcept {
    SecureZeroMemory(association.protection.key.data(), association.protection.key.size());
    SecureZeroMemory(association.protection.outboundBase.data(),
                     association.protection.outboundBase.size());
    SecureZeroMemory(association.protection.inboundBase.data(),
                     association.protection.inboundBase.size());
    association = {};
}

/**
 * Finds the association for one endpoint.
 * @param from Source endpoint.
 * @return Matching association, or null when the endpoint has none.
 */
[[nodiscard]] state::gameplay::Association*
find_locked(const state::gameplay::Endpoint& from) noexcept {
    for (state::gameplay::Association& association : g_state.associations) {
        if (association.stage != state::gameplay::AssociationStage::absent
            && association.endpoint == from) {
            return &association;
        }
    }
    return nullptr;
}

/** @return A free association slot, or null when every slot is taken. */
[[nodiscard]] state::gameplay::Association* allocate_locked() noexcept {
    for (state::gameplay::Association& association : g_state.associations) {
        if (association.stage == state::gameplay::AssociationStage::absent) {
            return &association;
        }
    }
    return nullptr;
}

/**
 * Fills in the response identity and key material for one accepted offer.
 * Every field is produced before anything is stored, so a failure leaves the association alone.
 * @param offer Accepted offer.
 * @param response Receives the complete response packet.
 * @param context Receives the crypto state to install.
 * @return True when the exchange and every generated field succeeded.
 */
[[nodiscard]] bool build_response(const wire::ControlPacket& offer,
                                  wire::ControlPacket& response,
                                  state::gameplay::ProtectedContext& context) noexcept {
    srp::Exchange exchange{};
    exchange.verifier = offer.verifier;
    exchange.clientPublic = offer.publicValue;
    if (!srp::derive(exchange)) {
        return false;
    }
    response = {};
    response.opcode = wire::Opcode::keyExchangeResponse;
    // The response echoes the offer's word B so the initiator can match it to its attempt.
    response.responseValue = offer.wordB;
    response.trailer = offer.trailer;
    response.hasSignOnBlob = false;
    response.publicValue = exchange.serverPublic;
    if (!middleware::crypto::random::fill(response.networkId)
        || !middleware::crypto::random::fill(response.identityNonce)
        || !middleware::crypto::random::fill(response.verifier)
        || !middleware::crypto::random::fill(response.seed)) {
        return false;
    }
    context = {};
    for (std::size_t index = 0; index < context.key.size(); ++index) {
        context.key[index] = exchange.derivedKey[index];
    }
    context.outboundBase = response.seed;
    context.inboundBase = offer.seed;
    context.addressTrailer = offer.trailer;
    context.outboundWordA = offer.wordA;
    context.outboundWordB = offer.wordB;
    context.installed = true;
    response.wordA = offer.wordA;
    response.wordB = offer.wordB;
    return true;
}

/**
 * Handles one key-exchange offer.
 * @param from Source endpoint.
 * @param offer Decoded offer.
 * @param now Monotonic tick count.
 */
void accept_offer(const state::gameplay::Endpoint& from,
                  const wire::ControlPacket& offer,
                  std::uint64_t now) noexcept {
    wire::ControlPacket response{};
    state::gameplay::ProtectedContext context{};
    bool answered = false;

    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::Association* association = find_locked(from);
    if (association != nullptr && association->answered && offer.wordB <= association->offerWordB) {
        // A retry keeps its word B, and the native responder does not answer it a second time.
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::debug,
               "ev=gameplay stage=offer result=drop reason=stale wordb=%u",
               offer.wordB);
        return;
    }
    if (association == nullptr) {
        association = allocate_locked();
    }
    if (association == nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        report(core::log::Level::warn, "ev=gameplay stage=offer result=fail reason=capacity");
        return;
    }
    ReleaseSRWLockExclusive(&g_lock);

    // The exchange runs outside the lock because it is far slower than any packet path.
    if (!build_response(offer, response, context)) {
        report(core::log::Level::warn, "ev=gameplay stage=offer result=fail reason=exchange");
        return;
    }

    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::Association* target = find_locked(from);
    if (target == nullptr) {
        target = allocate_locked();
    }
    if (target != nullptr && (!target->answered || offer.wordB > target->offerWordB)) {
        // Identity, words, and key material become visible together or not at all.
        target->endpoint = from;
        target->protection = context;
        target->stage = state::gameplay::AssociationStage::established;
        target->offerWordA = offer.wordA;
        target->offerWordB = offer.wordB;
        target->answered = true;
        target->acceptedWordA = 0;
        target->acceptedAny = false;
        target->createdTick = now;
        target->lastTick = now;
        answered = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!answered) {
        return;
    }

    std::array<std::byte, wire::kControlCapacity> datagram{};
    std::size_t size = 0;
    if (!wire::encode(response, datagram, size)
        || !endpoint::send_to(from, {datagram.data(), size})) {
        report(core::log::Level::warn, "ev=gameplay stage=response result=fail reason=send");
        return;
    }
    report(core::log::Level::info,
           "ev=gameplay stage=response result=ok wordb=%u signon=%u",
           offer.wordB,
           offer.hasSignOnBlob ? 1U : 0U);
}

/**
 * Handles one established protected datagram.
 * @param from Source endpoint.
 * @param datagram Whole received payload.
 * @param now Monotonic tick count.
 */
void accept_protected(const state::gameplay::Endpoint& from,
                      std::span<const std::byte> datagram,
                      std::uint64_t now) noexcept {
    state::gameplay::ProtectedContext context{};
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::Association* association = find_locked(from);
    const bool active =
        association != nullptr
        && (association->stage == state::gameplay::AssociationStage::pending
            || association->stage == state::gameplay::AssociationStage::established);
    if (active) {
        context = association->protection;
    }
    ReleaseSRWLockShared(&g_lock);
    if (!active) {
        return;
    }

    std::array<std::byte, wire::kMaximumPayload> payload{};
    std::size_t payloadSize = 0;
    std::uint32_t wordA = 0;
    if (!wire::open(context, datagram, payload, payloadSize, wordA)) {
        report(core::log::Level::debug, "ev=gameplay stage=protected result=drop reason=tag");
        return;
    }

    bool fresh = false;
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::Association* target = find_locked(from);
    if (target != nullptr
        && (target->stage == state::gameplay::AssociationStage::pending
            || target->stage == state::gameplay::AssociationStage::established)) {
        // Replay policy: word A must strictly increase.
        fresh = !target->acceptedAny || wordA > target->acceptedWordA;
        if (fresh) {
            target->acceptedWordA = wordA;
            target->acceptedAny = true;
            target->stage = state::gameplay::AssociationStage::established;
            target->lastTick = now;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!fresh) {
        report(core::log::Level::debug, "ev=gameplay stage=protected result=drop reason=replay");
        return;
    }
    peer::deliver(from, {payload.data(), payloadSize}, now);
}

} // namespace

/** Routes one received datagram by its outer marker. */
void route(const state::gameplay::Endpoint& from,
           std::span<const std::byte> datagram,
           std::uint64_t now) noexcept {
    if (datagram.size() < kMinimumDatagram) {
        return;
    }
    AcquireSRWLockShared(&g_lock);
    const state::gameplay::Association* current = find_locked(from);
    const bool established =
        current != nullptr && current->stage == state::gameplay::AssociationStage::established;
    const bool pending =
        current != nullptr && current->stage == state::gameplay::AssociationStage::pending;
    ReleaseSRWLockShared(&g_lock);
    if (established) {
        accept_protected(from, datagram, now);
        return;
    }
    wire::ControlPacket packet{};
    if (!wire::decode(datagram, packet)) {
        if (pending) {
            accept_protected(from, datagram, now);
            return;
        }
        report(core::log::Level::debug, "ev=gameplay stage=control result=drop reason=decode");
        return;
    }
    if (packet.opcode == wire::Opcode::keyExchangeOffer) {
        accept_offer(from, packet, now);
        return;
    }
    report(core::log::Level::debug,
           "ev=gameplay stage=control result=ignore opcode=%u",
           static_cast<unsigned>(packet.opcode));
}

/** Seals one transport payload and sends it to an established association. */
bool send_payload(const state::gameplay::Endpoint& to,
                  std::span<const std::byte> payload) noexcept {
    std::array<std::byte, wire::kProtectedCapacity> datagram{};
    std::size_t size = 0;
    AcquireSRWLockExclusive(&g_lock);
    state::gameplay::Association* association = find_locked(to);
    // Sealing runs under the lock because it advances the association's own send word.
    const bool sealed = association != nullptr
                        && association->stage == state::gameplay::AssociationStage::established
                        && wire::seal(association->protection, payload, datagram, size);
    ReleaseSRWLockExclusive(&g_lock);
    return sealed && endpoint::send_to(to, {datagram.data(), size});
}

/** Tears down associations that never completed. */
void expire(std::uint64_t now) noexcept {
    std::array<state::gameplay::Endpoint, state::gameplay::kAssociationCapacity> dropped{};
    std::size_t count = 0;
    AcquireSRWLockExclusive(&g_lock);
    for (state::gameplay::Association& association : g_state.associations) {
        const bool live = association.stage != state::gameplay::AssociationStage::absent;
        if (live && now - association.lastTick > state::gameplay::kAssociationTimeoutMs) {
            dropped[count] = association.endpoint;
            ++count;
            drop_locked(association);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    // Peer teardown runs outside the lock because it takes its own.
    for (std::size_t index = 0; index < count; ++index) {
        peer::drop_endpoint(dropped[index]);
        // The admitted records and their activity host sessions go with the link. Nothing else
        // reclaims them when the peer never sends a leave.
        group::release_endpoint(dropped[index]);
        report(core::log::Level::info, "ev=gameplay stage=association result=timeout");
    }
}

/** Drops every association and clears its key material. */
void reset() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    for (state::gameplay::Association& association : g_state.associations) {
        drop_locked(association);
    }
    g_state.endpointBound = false;
    g_state.bound = {};
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace sunrise::server::gameplay::association
