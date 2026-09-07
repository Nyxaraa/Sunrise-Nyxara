#pragma once

#include <array>
#include <cstdint>

namespace sunrise::state::gameplay::external {

/** Outbound external records use the transport packet ring size. */
inline constexpr std::size_t kExternalContributionCapacity = 128;

/** Protected carrier used by one peer generation. */
enum class CarrierKind : std::uint8_t {
    engineAssociation,
    dtls,
};

/** Identity of one established protected carrier. */
struct CarrierKey {
    CarrierKind kind{};
    std::uint64_t generation{};
};

/** Exact peer, view, activity, and channel tuple for one observation. */
struct ExternalShadowKey {
    CarrierKey carrier{};
    std::uint64_t authenticatedMemberId{};
    std::uint64_t peerGeneration{};
    std::uint64_t channelGeneration{};
    std::uint64_t groupSessionId{};
    std::uint64_t viewGeneration{};
    std::uint64_t activitySessionId{};
    std::uint32_t remoteConnectionSequence{};
    std::uint32_t localConnectionSequence{};
};

/** Receive-only common state retained for one exact context. */
struct ExternalShadow {
    ExternalShadowKey key{};
    std::array<std::uint64_t, 2> patchEpoch{};
    std::uint8_t reconciliationGeneration{};
    std::uint8_t currentCell{};
    bool currentCellPresent{};
    bool occupied{};
};

/** Immutable external state staged in one outbound packet. */
struct ExternalContributionSnapshot {
    std::uint64_t transmissionId{};
    std::uint64_t groupSessionId{};
    std::uint64_t viewGeneration{};
    std::uint16_t packetSequence{};
    bool commonPresent{};
    bool lane0Present{};
    bool occupied{};
};

} // namespace sunrise::state::gameplay::external
