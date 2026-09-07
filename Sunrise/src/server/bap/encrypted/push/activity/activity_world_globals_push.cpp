#include "activity_world_globals_push.h"

#include <Windows.h>

#include <bit>

#include "../../../../../middleware/bap/activity_message/world_globals_state.h"
#include "../../../../../middleware/secure_channel/runtime.h"
#include "activity_notification_frame.h"

namespace sunrise::server::bap::encrypted::push::activity {

namespace message = middleware::bap::activity_message::world_globals_state;

namespace {

/**
 * Activity-clock enable. The client reads only whether this real32 is above zero, at
 * `0x7FF74208AF64` and `0x7FF74208B07B`, and the magnitude never enters its arithmetic.
 * A NaN is refused by both of those tests in opposite directions, so the value must be finite.
 */
constexpr float kActivityClockEnabled = 1.0F;
static_assert(kActivityClockEnabled > 0.0F, "a clock that does not run is the shipping defect");

/** The companion bool has no reader in the client's activity-clock functions, so it stays clear. */
constexpr bool kWorldGlobalsFlag = false;

} // namespace

/** Appends one world-globals-state svc9 notification and advances its local nonce. */
bool append_world_globals_notification(Scratch& scratch,
                                       std::uint64_t sessionId,
                                       std::span<const std::byte, state::kAesKeySize> key,
                                       std::array<std::byte, state::kBapNonceSize>& nonce,
                                       std::span<std::byte> response,
                                       std::size_t& written) noexcept {
    if (written > response.size()) {
        return false;
    }

    const std::size_t initialWritten = written;
    auto initialNonce = nonce;
    message::Body body{};
    body.flag = kWorldGlobalsFlag;
    body.real32Bits = std::bit_cast<std::uint32_t>(kActivityClockEnabled);
    std::size_t messageSize = 0;
    const bool encoded =
        message::encode(body, scratch.responseBody, messageSize)
        && append_notification_frame(scratch,
                                     sessionId,
                                     message::kMessageType,
                                     std::span(scratch.responseBody).first(messageSize),
                                     key,
                                     nonce,
                                     response,
                                     written);
    SecureZeroMemory(scratch.responseBody.data(), messageSize);
    if (encoded) {
        middleware::secure_channel::advance_nonce(nonce);
    } else {
        if (written > initialWritten) {
            SecureZeroMemory(response.data() + initialWritten, written - initialWritten);
        }
        written = initialWritten;
        nonce = initialNonce;
    }
    SecureZeroMemory(&initialNonce, sizeof initialNonce);
    return encoded;
}

} // namespace sunrise::server::bap::encrypted::push::activity
