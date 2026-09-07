#include "activity_incident_push.h"

#include <Windows.h>

#include <algorithm>
#include <array>

#include "../../../../../middleware/bap/activity_message/incident.h"
#include "../../../../../middleware/encoding/bit_writer.h"
#include "../../../../../middleware/secure_channel/runtime.h"
#include "activity_notification_frame.h"

namespace sunrise::server::bap::encrypted::push::activity {
namespace {

namespace host = server::activity::host;
namespace incident = middleware::bap::activity_message::incident;
namespace bits = middleware::encoding::bits;

/** Wipes a caller-owned prefix that held an encoded incident body. */
void clear_prefix(std::span<std::byte> buffer, std::size_t size) noexcept {
    SecureZeroMemory(buffer.data(), (std::min)(buffer.size(), size));
}

} // namespace

/** Appends one pending operator incident on a local nonce transaction. */
bool append_incident_notification(Session& session,
                                  Scratch& scratch,
                                  const host::PendingIncident& pending,
                                  std::span<const std::byte, state::kAesKeySize> key,
                                  std::array<std::byte, state::kBapNonceSize>& nonce,
                                  std::span<std::byte> response,
                                  std::size_t& written) noexcept {
    discard_staged_incident(session);
    if (pending.revision == 0 || written > response.size()) {
        return false;
    }
    const std::size_t initialWritten = written;
    auto initialNonce = nonce;
    std::array<std::byte, incident::kMaximumBodyBytes> body{};
    bits::Writer writer(body);
    std::size_t bodySize = 0;
    const bool bodyEncoded = incident::write(writer, pending.incident) && writer.finish(bodySize);
    bool encoded = bodyEncoded
                   && append_notification_frame(scratch,
                                                session.activity.session.sessionId,
                                                incident::kMessageType,
                                                std::span(body).first(bodySize),
                                                key,
                                                nonce,
                                                response,
                                                written);
    if (encoded) {
        middleware::secure_channel::advance_nonce(nonce);
        session.activityIncidentStaged.bindingGeneration = session.activity.bindingGeneration;
        session.activityIncidentStaged.revision = pending.revision;
        session.activityIncidentStaged.staged = true;
    } else {
        clear_prefix(response.subspan(initialWritten), written - initialWritten);
        written = initialWritten;
        nonce = initialNonce;
        host::note_incident_attempt(session.activity.session,
                                    session.activity.bindingGeneration,
                                    pending.revision,
                                    bodyEncoded ? host::IncidentStatus::frameRefused
                                                : host::IncidentStatus::encodeFailed);
    }
    clear_prefix(body, bodySize);
    SecureZeroMemory(&initialNonce, sizeof initialNonce);
    return encoded;
}

/** Clears an encoded incident that did not reach the transport caller. */
void discard_staged_incident(Session& session) noexcept {
    const IncidentPublication staged = session.activityIncidentStaged;
    session.activityIncidentStaged = {};
    if (staged.staged && staged.bindingGeneration == session.activity.bindingGeneration) {
        host::note_incident_attempt(session.activity.session,
                                    staged.bindingGeneration,
                                    staged.revision,
                                    host::IncidentStatus::frameRefused);
    }
}

/** Advances the exact host output after its complete encrypted frame reaches the caller. */
void commit_staged_incident(Session& session) noexcept {
    const IncidentPublication staged = session.activityIncidentStaged;
    session.activityIncidentStaged = {};
    if (!staged.staged || staged.bindingGeneration != session.activity.bindingGeneration) {
        return;
    }
    host::note_incident_transport_staged(
        session.activity.session, staged.bindingGeneration, staged.revision);
}

} // namespace sunrise::server::bap::encrypted::push::activity
