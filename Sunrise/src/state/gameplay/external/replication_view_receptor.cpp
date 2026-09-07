#include "replication_view_receptor.h"

namespace sunrise::state::gameplay::external::view_receptor {
namespace {

/** The server publishes stage 1 and waits for the client's stage 2. */
constexpr std::uint8_t kInitialStage = 1;
/** Stage 2 is the only body that carries the client-selected signature. */
constexpr std::uint8_t kSignatureStage = 2;
/** Both halves at stage 4 may publish a provisional replication view. */
constexpr std::uint8_t kProvisionalStage = 4;
/** Both halves at stage 5 fully accept the replication view. */
constexpr std::uint8_t kAcceptedStage = 5;

} // namespace

/** Clears the negotiation and every generation-qualified field. */
void Receptor::reset() noexcept {
    *this = {};
    viewIndex_ = -1;
}

/** Opens one receptor and stages its initial stage-1 publication. */
bool Receptor::open(std::uint64_t token, std::uint64_t generation) noexcept {
    reset();
    if (token == 0 || generation == 0) {
        return false;
    }
    token_ = token;
    generation_ = generation;
    pendingStage_ = kInitialStage;
    occupied_ = true;
    return true;
}

/** Opens one receptor after validating the peer's first stage. */
bool Receptor::open_from_stage_one(const middleware::gameplay::group::ViewEstablishment& input,
                                   std::uint64_t generation) noexcept {
    reset();
    if (input.kind != kInitialStage || input.sessionToken == 0 || generation == 0
        || input.hasOptionalValue || input.hasList || input.listCount != 0) {
        return false;
    }
    token_ = input.sessionToken;
    generation_ = generation;
    remoteStage_ = kInitialStage;
    pendingStage_ = kInitialStage;
    occupied_ = true;
    return true;
}

/** Copies the currently owed response. */
bool Receptor::pending(middleware::gameplay::group::ViewEstablishment& output) const noexcept {
    if (!occupied_ || failed_ || pendingStage_ == 0) {
        return false;
    }
    output = body(pendingStage_);
    return true;
}

/** Commits the pending local stage after its reliable enqueue succeeds. */
bool Receptor::commit_pending() noexcept {
    if (!occupied_ || failed_ || pendingStage_ == 0) {
        return false;
    }
    localStage_ = pendingStage_;
    pendingStage_ = 0;
    return true;
}

/** Rebuilds the only transition whose duplicate is accepted by the initiator. */
bool Receptor::retry_stage_one(
    middleware::gameplay::group::ViewEstablishment& output) const noexcept {
    if (!occupied_ || failed_ || pendingStage_ != 0 || localStage_ != kInitialStage
        || remoteStage_ != 0) {
        return false;
    }
    output = body(kInitialStage);
    return true;
}

/** Validates and retains the next client transition. */
ReceiveResult
Receptor::receive(const middleware::gameplay::group::ViewEstablishment& input) noexcept {
    if (!occupied_ || failed_) {
        return ReceiveResult::notOpen;
    }
    if (input.sessionToken != token_) {
        return ReceiveResult::wrongToken;
    }
    if (pendingStage_ != 0) {
        return ReceiveResult::responsePending;
    }

    const std::uint8_t expected =
        remoteStage_ == 0 ? kSignatureStage : static_cast<std::uint8_t>(remoteStage_ + 1U);
    if (expected > kAcceptedStage || input.kind != expected || localStage_ + 1U != expected) {
        return fail(ReceiveResult::invalidStage);
    }
    if (!input.hasOptionalValue || input.optionalValue < 0) {
        return fail(ReceiveResult::invalidIndex);
    }

    if (expected == kSignatureStage) {
        if (!input.hasList || input.listCount > input.list.size()) {
            return fail(ReceiveResult::invalidSignature);
        }
        viewIndex_ = input.optionalValue;
        signatureCount_ = input.listCount;
        signature_ = input.list;
        signatureAdopted_ = true;
    } else {
        if (input.optionalValue != viewIndex_) {
            return fail(ReceiveResult::invalidIndex);
        }
        if (input.hasList || input.listCount != 0) {
            return fail(ReceiveResult::invalidSignature);
        }
    }

    remoteStage_ = expected;
    pendingStage_ = expected;
    return ReceiveResult::accepted;
}

/** Native publication starts at the peer's stage 3, before its stage-4 transition. */
bool Receptor::accepts_inbound_entities() const noexcept {
    return occupied_ && !failed_ && signatureAdopted_ && viewIndex_ >= 0
           && localStage_ >= kSignatureStage && remoteStage_ >= 3;
}

/** Returns the current externally useful negotiation phase. */
Phase Receptor::phase() const noexcept {
    if (!occupied_) {
        return Phase::closed;
    }
    if (failed_) {
        return Phase::failed;
    }
    if (localStage_ >= kAcceptedStage && remoteStage_ >= kAcceptedStage) {
        return Phase::accepted;
    }
    if (localStage_ >= kProvisionalStage && remoteStage_ >= kProvisionalStage) {
        return Phase::provisional;
    }
    return Phase::negotiating;
}

/** Returns the process-local generation that owns this negotiation. */
std::uint64_t Receptor::generation() const noexcept {
    return generation_;
}

/** Returns the client-chosen view index, or -1 before stage 2. */
std::int32_t Receptor::view_index() const noexcept {
    return viewIndex_;
}

/** Returns the last committed local stage. */
std::uint8_t Receptor::local_stage() const noexcept {
    return localStage_;
}

/** Returns the last retained remote stage. */
std::uint8_t Receptor::remote_stage() const noexcept {
    return remoteStage_;
}

/** Builds one stage from retained token, index, and signature state. */
middleware::gameplay::group::ViewEstablishment Receptor::body(std::uint8_t stage) const noexcept {
    middleware::gameplay::group::ViewEstablishment output{};
    output.kind = stage;
    output.sessionToken = token_;
    if (stage >= kSignatureStage) {
        output.hasOptionalValue = true;
        output.optionalValue = viewIndex_;
    }
    if (stage == kSignatureStage && signatureAdopted_) {
        output.hasList = true;
        output.listCount = signatureCount_;
        output.list = signature_;
    }
    return output;
}

/** Fails the current receptor and returns the supplied reason. */
ReceiveResult Receptor::fail(ReceiveResult reason) noexcept {
    failed_ = true;
    pendingStage_ = 0;
    return reason;
}

} // namespace sunrise::state::gameplay::external::view_receptor
