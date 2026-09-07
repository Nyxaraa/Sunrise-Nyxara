#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../../../state/build_data/scriptables/definition.h"

namespace sunrise::client::content::activity::scriptables::internal {

/** Hard bound for package-authored placement rows in one transient scenario catalog. */
inline constexpr std::size_t kAuthoredPlacementCapacity = 262'144;

/** One object-list row retained with its package-declared bubble context. */
struct RawAuthoredPlacement final {
    std::int32_t declaredBubbleIndex{};
    std::uint32_t objectListTag{};
    std::uint32_t classListTag{};
    std::uint32_t entryIndex{};
    std::array<float, 4> rotation{};
    std::array<float, 3> position{};
    std::uint64_t sourceOffset{};
    std::uint64_t identifier{};
    std::int64_t auxiliaryRelative{};
    std::array<std::uint32_t, 4> rotationBits{};
    std::array<std::uint32_t, 3> positionBits{};
    float uniformScale{};
    std::uint32_t uniformScaleBits{};
    std::uint32_t nameHash{};
    std::uint32_t placementFlagsRaw{};
};

/** Deduplicated authored-list traversal state for one package object definition. */
struct AuthoredPlacementAnalysis final {
    struct List final {
        std::int32_t declaredBubbleIndex{};
        std::uint32_t objectListTag{};
    };

    std::vector<RawAuthoredPlacement> placements{};
    std::vector<List> lists{};
};

/** Reads one explicit object list once for each package-declared bubble context. */
[[nodiscard]] bool collect_authored_placements(AuthoredPlacementAnalysis& analysis,
                                               std::span<const std::byte> blob,
                                               std::uint32_t objectListTag,
                                               std::int32_t declaredBubbleIndex) noexcept;

/** @return True when one package-declared placement context belongs to a slice-set state. */
[[nodiscard]] bool placement_applies(const RawAuthoredPlacement& placement,
                                     std::uint32_t sliceSetIndex) noexcept;

/** Appends the anchors applicable to one exact catalog object/state placement. */
[[nodiscard]] bool
append_authored_placements(const AuthoredPlacementAnalysis& analysis,
                           std::uint32_t sourceObjectRow,
                           state::build_data::scriptables::Snapshot& output) noexcept;

} // namespace sunrise::client::content::activity::scriptables::internal
