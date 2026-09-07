#include "activity_authority_query_owner.h"

#include <bit>
#include <limits>

namespace sunrise::server::bap::authority_query {
namespace {

namespace authority = middleware::bap::activity_message::entity_authority;

/** The client answers on its next view tick; this bound permits a full activity load stall. */
constexpr std::uint64_t kResponseTimeoutMs = 20'000;

/** Advances through every signed token value except the client's -1 idle sentinel. */
[[nodiscard]] std::int32_t reserve_correlation(Owner& owner) noexcept {
    std::int32_t reserved = owner.nextCorrelation;
    if (reserved == -1) {
        reserved = 0;
    }
    std::uint32_t next = std::bit_cast<std::uint32_t>(reserved) + 1U;
    if (next == (std::numeric_limits<std::uint32_t>::max)()) {
        next = 0;
    }
    owner.nextCorrelation = std::bit_cast<std::int32_t>(next);
    return reserved;
}

/** Adds a timeout interval without wrapping the monotonic tick count. */
[[nodiscard]] std::uint64_t deadline_from(std::uint64_t now) noexcept {
    const std::uint64_t remaining = (std::numeric_limits<std::uint64_t>::max)() - now;
    return remaining < kResponseTimeoutMs ? (std::numeric_limits<std::uint64_t>::max)()
                                          : now + kResponseTimeoutMs;
}

} // namespace

/** Cancels one old binding while preserving the connection's fresh-token sequence. */
void reset(Owner& owner, std::uint64_t bindingGeneration) noexcept {
    const std::int32_t next = owner.nextCorrelation;
    owner = {};
    owner.bindingGeneration = bindingGeneration;
    owner.correlation = -1;
    owner.nextCorrelation = next == -1 ? 0 : next;
}

/** Opens one bounded query and allocates its non--1 correlation value. */
ActivityAuthorityQueryStatus
request(Owner& owner, std::uint64_t bindingGeneration, std::int32_t& correlation) noexcept {
    correlation = -1;
    if (bindingGeneration == 0 || owner.bindingGeneration != bindingGeneration) {
        return ActivityAuthorityQueryStatus::staleActivityClient;
    }
    if (owner.phase == Phase::pending || owner.phase == Phase::awaiting || owner.staged) {
        return ActivityAuthorityQueryStatus::busy;
    }
    owner.snapshot = {};
    owner.deadlineTick = 0;
    owner.correlation = reserve_correlation(owner);
    owner.phase = Phase::pending;
    owner.staged = false;
    correlation = owner.correlation;
    return ActivityAuthorityQueryStatus::queued;
}

/** Checks whether the exact binding has one msg-30 body waiting to be sent. */
bool pending(const Owner& owner, std::uint64_t bindingGeneration) noexcept {
    return bindingGeneration != 0 && owner.bindingGeneration == bindingGeneration
           && owner.phase == Phase::pending && !owner.staged && owner.correlation != -1;
}

/** Marks the current pending query as present in a staged transport frame. */
bool stage(Owner& owner, std::uint64_t bindingGeneration, std::int32_t& correlation) noexcept {
    correlation = -1;
    if (bindingGeneration == 0 || owner.bindingGeneration != bindingGeneration
        || owner.phase != Phase::pending || owner.staged || owner.correlation == -1) {
        return false;
    }
    owner.staged = true;
    correlation = owner.correlation;
    return true;
}

/** Starts the response timeout after the complete msg-30 frame reaches the caller. */
void commit_staged(Owner& owner, std::uint64_t bindingGeneration, std::uint64_t now) noexcept {
    if (bindingGeneration == 0 || owner.bindingGeneration != bindingGeneration || !owner.staged
        || owner.phase != Phase::pending) {
        return;
    }
    owner.staged = false;
    owner.phase = Phase::awaiting;
    owner.deadlineTick = deadline_from(now);
}

/** Returns a staged msg-30 to pending when its frame is refused. */
void discard_staged(Owner& owner, std::uint64_t bindingGeneration) noexcept {
    if (owner.bindingGeneration == bindingGeneration && owner.phase == Phase::pending) {
        owner.staged = false;
    }
}

/** Applies one correlated response before its closed deadline. */
AnswerStatus submit(Owner& owner,
                    std::uint64_t bindingGeneration,
                    std::uint64_t now,
                    const authority::QueryAnswer& answer) noexcept {
    if (bindingGeneration == 0 || owner.bindingGeneration != bindingGeneration) {
        return AnswerStatus::staleBinding;
    }
    if (owner.phase != Phase::awaiting) {
        return AnswerStatus::noQuery;
    }
    if (now >= owner.deadlineTick) {
        owner.deadlineTick = 0;
        owner.phase = Phase::timedOut;
        return AnswerStatus::timedOut;
    }
    if (answer.correlation != owner.correlation) {
        return AnswerStatus::tokenMismatch;
    }
    if (answer.hasSelector) {
        // Only the selector bound is checked. Mask content, arrival order, and repeats are the
        // client's to choose; a repeat overwrites its own row.
        if (!answer.hasMask
            || answer.selector >= static_cast<std::uint8_t>(owner.snapshot.bubbleMasks.size())) {
            return AnswerStatus::invalidShape;
        }
        if (!owner.snapshot.bubblePresent[answer.selector]) {
            owner.snapshot.bubblePresent[answer.selector] = true;
            ++owner.snapshot.bubbleResponseCount;
        }
        owner.snapshot.bubbleMasks[answer.selector] = answer.mask;
        owner.snapshot.lastBubble = answer.selector;
        return AnswerStatus::bubbleAccepted;
    }
    if (!answer.hasMask) {
        return AnswerStatus::invalidShape;
    }
    owner.snapshot.authorityMask = answer.mask;
    owner.snapshot.bindingGeneration = owner.bindingGeneration;
    owner.snapshot.correlation = owner.correlation;
    owner.snapshot.complete = true;
    owner.deadlineTick = 0;
    owner.phase = Phase::complete;
    return AnswerStatus::complete;
}

/** Closes an unanswered query once its bounded wait has elapsed. */
bool expire(Owner& owner, std::uint64_t bindingGeneration, std::uint64_t now) noexcept {
    if (bindingGeneration == 0 || owner.bindingGeneration != bindingGeneration
        || owner.phase != Phase::awaiting || now < owner.deadlineTick) {
        return false;
    }
    owner.deadlineTick = 0;
    owner.phase = Phase::timedOut;
    return true;
}

/** Copies one complete stable result for a later API consumer. */
ActivityAuthorityQueryStatus snapshot(const Owner& owner,
                                      std::uint64_t bindingGeneration,
                                      ActivityAuthorityQuerySnapshot& output) noexcept {
    output = {};
    if (bindingGeneration == 0 || owner.bindingGeneration != bindingGeneration) {
        return ActivityAuthorityQueryStatus::staleActivityClient;
    }
    switch (owner.phase) {
    case Phase::pending:
    case Phase::awaiting:
        return ActivityAuthorityQueryStatus::pending;
    case Phase::complete:
        output = owner.snapshot;
        return ActivityAuthorityQueryStatus::complete;
    case Phase::timedOut:
        return ActivityAuthorityQueryStatus::timedOut;
    case Phase::idle:
        return ActivityAuthorityQueryStatus::noResult;
    }
    return ActivityAuthorityQueryStatus::noResult;
}

} // namespace sunrise::server::bap::authority_query
