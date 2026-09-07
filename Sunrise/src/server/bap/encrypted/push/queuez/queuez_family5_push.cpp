#include <array>
#include <span>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/datagen/definitions.h"
#include "../../../../../middleware/queuez/queuez_update.h"
#include "../../../../../middleware/web_service/messages/family5_codec.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../../../web_service/web_service_runtime.h"
#include "queuez_update_frame.h"

namespace sunrise::server::bap::encrypted::push {

/** Appends the global family-five unlock overrides as one full-snapshot notification. */
bool append_family5_override_notification(Scratch& scratch,
                                          std::int32_t version,
                                          std::span<const std::byte, state::kAesKeySize> key,
                                          std::span<const std::byte, state::kBapNonceSize> nonce,
                                          std::span<std::byte> response,
                                          std::size_t& written) noexcept {
    namespace family5 = middleware::web_service::messages::family5;
    static_assert(client::network::kBapFrameCapacity >= family5::kObjectCapacity);
    state::InvestmentState investment{};
    if (!state::investment_snapshot(investment)
        || investment.family5.objectSoid != middleware::datagen::kUnlockSentinelSoid) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=family5_overrides result=fail reason=state");
        return false;
    }
    const auto body = std::span(scratch.plaintext).first(family5::kObjectCapacity);
    std::size_t bodySize = 0;
    // An empty payload is a delete on the wire, so it must never reach the object header.
    if (!family5::encode_object(
            investment.family5, web_service::next_family5_clock(), body, bodySize)
        || bodySize == 0) {
        queuez_frame::clear_object_storage(scratch, family5::kObjectCapacity, 0);
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=queuez stage=family5_overrides result=fail reason=encode");
        return false;
    }
    // The Client decodes this payload with the slot's own schema, so it goes out tag-reflected.
    // A raw image would have to carry the whole 1712-byte object, which nothing here authors.
    const std::array objects{middleware::queuez::Object{
        middleware::datagen::kUnlockObjectId,
        middleware::datagen::kUnlockSentinelSoid,
        middleware::queuez::Encoding::tagReflection,
        body.first(bodySize),
    }};
    // Always a full snapshot. The record stays SUBSCRIBED until one lands, and a resnapshot keeps
    // every object the same frame names.
    const middleware::queuez::Family family{
        middleware::datagen::kUnlockFamily,
        middleware::datagen::kUnlockSentinelSoid,
        version,
        middleware::queuez::kFullSnapshotFlag,
        objects,
    };
    return queuez_frame::append(
        scratch, family, family5::kObjectCapacity, 0, key, nonce, response, written);
}

} // namespace sunrise::server::bap::encrypted::push
