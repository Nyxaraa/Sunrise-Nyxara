#pragma once

#include <array>
#include <cstdint>

#include "../../middleware/bap/activity_message/entity_authority.h"
#include "activity_authority_query.h"

namespace sunrise::server::bap::authority_query {

/** One connection-local msg-30 exchange can be active at a time. */
enum class Phase : std::uint8_t {
    idle,
    pending,
    awaiting,
    complete,
    timedOut,
};

/** Result of applying one exact msg-31 or msg-32 body to its pending query. */
enum class AnswerStatus : std::uint8_t {
    bubbleAccepted,
    complete,
    timedOut,
    noQuery,
    staleBinding,
    tokenMismatch,
    invalidShape,
};

/** State retained by one authenticated ActivityClient link. */
struct Owner final {
    ActivityAuthorityQuerySnapshot snapshot{};
    std::uint64_t bindingGeneration{};
    std::uint64_t deadlineTick{};
    std::int32_t correlation{-1};
    std::int32_t nextCorrelation{};
    Phase phase{Phase::idle};
    bool staged{};
};

/** Cancels one old binding while preserving the connection's fresh-token sequence. */
void reset(Owner& owner, std::uint64_t bindingGeneration) noexcept;

/** Opens one bounded query and allocates its non--1 correlation value. */
[[nodiscard]] ActivityAuthorityQueryStatus
request(Owner& owner, std::uint64_t bindingGeneration, std::int32_t& correlation) noexcept;

/** @return True when the exact binding has one msg-30 body waiting to be sent. */
[[nodiscard]] bool pending(const Owner& owner, std::uint64_t bindingGeneration) noexcept;

/** Marks the current pending query as present in a staged transport frame. */
[[nodiscard]] bool
stage(Owner& owner, std::uint64_t bindingGeneration, std::int32_t& correlation) noexcept;

/** Starts the response timeout after the complete msg-30 frame reaches the caller. */
void commit_staged(Owner& owner, std::uint64_t bindingGeneration, std::uint64_t now) noexcept;

/** Returns a staged msg-30 to pending when its frame is refused. */
void discard_staged(Owner& owner, std::uint64_t bindingGeneration) noexcept;

/** Applies one correlated response before its closed deadline. */
[[nodiscard]] AnswerStatus
submit(Owner& owner,
       std::uint64_t bindingGeneration,
       std::uint64_t now,
       const middleware::bap::activity_message::entity_authority::QueryAnswer& answer) noexcept;

/** Closes an unanswered query once its bounded wait has elapsed. */
[[nodiscard]] bool
expire(Owner& owner, std::uint64_t bindingGeneration, std::uint64_t now) noexcept;

/** Copies one complete stable result for a later API consumer. */
[[nodiscard]] ActivityAuthorityQueryStatus
snapshot(const Owner& owner,
         std::uint64_t bindingGeneration,
         ActivityAuthorityQuerySnapshot& output) noexcept;

} // namespace sunrise::server::bap::authority_query
