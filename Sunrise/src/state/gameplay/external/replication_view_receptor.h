#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../../middleware/gameplay/group/view_message.h"

namespace sunrise::state::gameplay::external::view_receptor {

/** One receptor's externally useful negotiation phase. */
enum class Phase : std::uint8_t {
    closed,
    negotiating,
    provisional,
    accepted,
    failed,
};

/** Result of consuming one client-initiated message-40 transition. */
enum class ReceiveResult : std::uint8_t {
    accepted,
    notOpen,
    responsePending,
    wrongToken,
    invalidStage,
    invalidIndex,
    invalidSignature,
};

/**
 * Server-side receptor for one stock replication-view negotiation.
 * Remote transitions are retained before their matching outbound response is committed. This
 * keeps a refused reliable enqueue pending without falsely advancing the local half.
 */
class Receptor final {
public:
    /** Clears the negotiation and every generation-qualified field. */
    void reset() noexcept;

    /**
     * Opens one receptor and stages its initial stage-1 publication.
     * @param token Native view token shared by every transition.
     * @param generation Process-local generation that owns this receptor.
     * @return True when both identities are non-zero and the receptor was opened.
     */
    [[nodiscard]] bool open(std::uint64_t token, std::uint64_t generation) noexcept;

    /** Opens a receptor from the peer's first stage-1 publication. */
    [[nodiscard]] bool
    open_from_stage_one(const middleware::gameplay::group::ViewEstablishment& input,
                        std::uint64_t generation) noexcept;

    /**
     * Copies the currently owed response.
     * @param output Receives the exact body to enqueue.
     * @return True while an uncommitted stage is pending.
     */
    [[nodiscard]] bool
    pending(middleware::gameplay::group::ViewEstablishment& output) const noexcept;

    /**
     * Commits the pending local stage after its reliable enqueue succeeds.
     * @return True when one pending response was committed.
     */
    [[nodiscard]] bool commit_pending() noexcept;

    /**
     * Rebuilds stage 1 for its allowed bounded retry after the first publication committed.
     * The retry changes no state and stages 2 through 5 are never rebuilt this way.
     */
    [[nodiscard]] bool
    retry_stage_one(middleware::gameplay::group::ViewEstablishment& output) const noexcept;

    /**
     * Validates and retains the next client transition, then stages the matching response.
     * A matching-token protocol error fails the receptor. A foreign token is ignored because the
     * native token lookup rejects it before reaching the receptor.
     */
    [[nodiscard]] ReceiveResult
    receive(const middleware::gameplay::group::ViewEstablishment& input) noexcept;

    /** @return True after the peer announces stage 3 with its validated signature. */
    [[nodiscard]] bool accepts_inbound_entities() const noexcept;

    /** @return Current externally useful negotiation phase. */
    [[nodiscard]] Phase phase() const noexcept;
    /** @return Process-local generation that owns this negotiation. */
    [[nodiscard]] std::uint64_t generation() const noexcept;
    /** @return Client-chosen view index, or -1 before stage 2. */
    [[nodiscard]] std::int32_t view_index() const noexcept;
    /** @return Last committed local stage. */
    [[nodiscard]] std::uint8_t local_stage() const noexcept;
    /** @return Last retained remote stage. */
    [[nodiscard]] std::uint8_t remote_stage() const noexcept;

private:
    /** Builds one stage from retained token, index, and signature state. */
    [[nodiscard]] middleware::gameplay::group::ViewEstablishment
    body(std::uint8_t stage) const noexcept;
    /** Fails the current receptor and returns the supplied reason. */
    [[nodiscard]] ReceiveResult fail(ReceiveResult reason) noexcept;

    std::array<std::byte, middleware::gameplay::group::kViewListCapacity> signature_{};
    std::uint64_t token_{};
    std::uint64_t generation_{};
    std::int32_t viewIndex_{-1};
    std::uint8_t signatureCount_{};
    std::uint8_t localStage_{};
    std::uint8_t remoteStage_{};
    std::uint8_t pendingStage_{};
    bool signatureAdopted_{};
    bool occupied_{};
    bool failed_{};
};

} // namespace sunrise::state::gameplay::external::view_receptor
