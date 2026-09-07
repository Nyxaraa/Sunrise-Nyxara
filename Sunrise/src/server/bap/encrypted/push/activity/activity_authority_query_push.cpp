#include "activity_authority_query_push.h"

#include <Windows.h>

#include <algorithm>
#include <array>

#include "../../../../../middleware/bap/activity_message/activity_host_control.h"
#include "../../../../../middleware/secure_channel/runtime.h"
#include "../../../activity_authority_query_owner.h"
#include "activity_notification_frame.h"

namespace sunrise::server::bap::encrypted::push::activity {
namespace {

namespace host_control = middleware::bap::activity_message::host_control;

/** Clears bytes appended after a local transport transaction began. */
void clear_tail(std::span<std::byte> response,
                std::size_t initialWritten,
                std::size_t written) noexcept {
    if (written > initialWritten && initialWritten <= response.size()) {
        SecureZeroMemory(response.data() + initialWritten,
                         (std::min)(written - initialWritten, response.size() - initialWritten));
    }
}

} // namespace

/** Appends one pending msg-30 query on a local nonce transaction. */
bool append_authority_query_notification(Session& session,
                                         Scratch& scratch,
                                         std::span<const std::byte, state::kAesKeySize> key,
                                         std::array<std::byte, state::kBapNonceSize>& nonce,
                                         std::span<std::byte> response,
                                         std::size_t& written) noexcept {
    discard_staged_authority_query(session);
    if (written > response.size()) {
        return false;
    }
    std::int32_t correlation = -1;
    if (!authority_query::stage(
            session.activityAuthorityQuery, session.activity.bindingGeneration, correlation)) {
        return false;
    }

    const std::size_t initialWritten = written;
    auto initialNonce = nonce;
    std::array<std::byte, host_control::kCorrelationByteCount> body{};
    std::size_t bodySize = 0;
    const host_control::AuthorityMaskRequestBody request{correlation};
    const bool encoded = host_control::encode_query_authority_mask(request, body, bodySize)
                         && append_notification_frame(scratch,
                                                      session.activity.session.sessionId,
                                                      host_control::kQueryAuthorityMaskMessageType,
                                                      std::span(body).first(bodySize),
                                                      key,
                                                      nonce,
                                                      response,
                                                      written);
    SecureZeroMemory(body.data(), body.size());
    if (encoded) {
        middleware::secure_channel::advance_nonce(nonce);
        SecureZeroMemory(&initialNonce, sizeof initialNonce);
        return true;
    }
    clear_tail(response, initialWritten, written);
    written = initialWritten;
    nonce = initialNonce;
    authority_query::discard_staged(session.activityAuthorityQuery,
                                    session.activity.bindingGeneration);
    SecureZeroMemory(&initialNonce, sizeof initialNonce);
    return false;
}

/** Returns a staged msg-30 to pending when its frame is refused. */
void discard_staged_authority_query(Session& session) noexcept {
    authority_query::discard_staged(session.activityAuthorityQuery,
                                    session.activity.bindingGeneration);
}

/** Starts the answer timeout after a staged msg-30 reaches the caller. */
void commit_staged_authority_query(Session& session, std::uint64_t now) noexcept {
    authority_query::commit_staged(
        session.activityAuthorityQuery, session.activity.bindingGeneration, now);
}

} // namespace sunrise::server::bap::encrypted::push::activity
