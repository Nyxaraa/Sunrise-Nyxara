#pragma once

#include <cstdint>

#include "../../middleware/bap/activity_message/entity_authority.h"
#include "activity_authority_reset.h"

namespace sunrise::server::bap::authority_reset {

/** One connection-local msg-28 exchange can be active at a time. */
enum class Phase : std::uint8_t {
    idle,
    pending,
    awaiting,
    complete,
    timedOut,
};

/** Result of applying one exact msg-29 body to its pending reset. */
enum class AcknowledgementStatus : std::uint8_t {
    complete,
    timedOut,
    noReset,
    staleBinding,
    tokenMismatch,
    invalidShape,
};

/** State retained by one authenticated ActivityClient link. */
struct Owner final {
    ActivityAuthorityResetSnapshot snapshot{};
    std::uint64_t bindingGeneration{};
    std::uint64_t deadlineTick{};
    std::int32_t correlation{-1};
    std::int32_t nextCorrelation{};
    Phase phase{Phase::idle};
    bool staged{};
};

/** Cancels one old binding while preserving the connection's fresh-token sequence. */
void reset(Owner& owner, std::uint64_t bindingGeneration) noexcept;

/** Opens one bounded reset and allocates its non--1 correlation value. */
[[nodiscard]] ActivityAuthorityResetStatus
request(Owner& owner, std::uint64_t bindingGeneration, std::int32_t& correlation) noexcept;

/** @return True when the exact binding has one msg-28 body waiting to be sent. */
[[nodiscard]] bool pending(const Owner& owner, std::uint64_t bindingGeneration) noexcept;

/** Marks the current pending reset as present in a staged transport frame. */
[[nodiscard]] bool
stage(Owner& owner, std::uint64_t bindingGeneration, std::int32_t& correlation) noexcept;

/** Starts the acknowledgement timeout after the complete msg-28 frame reaches the caller. */
void commit_staged(Owner& owner, std::uint64_t bindingGeneration, std::uint64_t now) noexcept;

/** Returns a staged msg-28 to pending when its frame is refused. */
void discard_staged(Owner& owner, std::uint64_t bindingGeneration) noexcept;

/** Applies one correlated acknowledgement before its closed deadline. */
[[nodiscard]] AcknowledgementStatus
submit(Owner& owner,
       std::uint64_t bindingGeneration,
       std::uint64_t now,
       const middleware::bap::activity_message::entity_authority::QueryAnswer& answer) noexcept;

/** Closes an unanswered reset once its bounded wait has elapsed. */
[[nodiscard]] bool
expire(Owner& owner, std::uint64_t bindingGeneration, std::uint64_t now) noexcept;

/** Copies one complete stable result for a later API consumer. */
[[nodiscard]] ActivityAuthorityResetStatus
snapshot(const Owner& owner,
         std::uint64_t bindingGeneration,
         ActivityAuthorityResetSnapshot& output) noexcept;

} // namespace sunrise::server::bap::authority_reset
