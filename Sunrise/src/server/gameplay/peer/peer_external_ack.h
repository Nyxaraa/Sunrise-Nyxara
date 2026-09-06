#pragma once
#include "../../../middleware/gameplay/peer/established_packet.h"

namespace sunrise::server::gameplay::peer {
struct CompletedExternal {
    std::uint64_t groupSessionId{};
    std::uint64_t transmissionId{};
    middleware::gameplay::peer::AckOutcome outcome{
        middleware::gameplay::peer::AckOutcome::unresolved};
};
using DisplacedExternals =
    std::array<CompletedExternal, state::gameplay::external::kExternalContributionCapacity>;

// Called after transport ownership/guard validation, independently of lane decoding.
[[nodiscard]] inline std::size_t acknowledge_external(
    state::gameplay::PeerLink& peer,
    const middleware::gameplay::peer::AckState& ack,
    DisplacedExternals& completed) noexcept {
    namespace wire = middleware::gameplay::peer;
    std::size_t count = 0;
    for (auto& contribution : peer.externalContributions) {
        if (!contribution.occupied) continue;
        const auto outcome = wire::acknowledgement_outcome(ack, contribution.packetSequence);
        if (outcome == wire::AckOutcome::unresolved) continue;
        completed[count++] = {contribution.groupSessionId, contribution.transmissionId, outcome};
        if (outcome == wire::AckOutcome::received && contribution.commonPresent
            && contribution.viewGeneration == peer.viewGeneration) peer.commonCommitted = true;
        contribution = {};
    }
    return count;
}
}
