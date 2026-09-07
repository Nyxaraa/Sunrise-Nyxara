#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "definition_index_table.h"

namespace sunrise::middleware::content::packages::tables {

/** Tag class of an authored object-placement list. */
inline constexpr std::uint32_t kAuthoredPlacementListClass = 0x808099D6U;
/** Leaf class whose bare form directly names an authored object-placement list. */
inline constexpr std::uint32_t kAuthoredPlacementLeafClass = 0x80809468U;
/** A placement list holds its entry array descriptor here. */
inline constexpr std::size_t kAuthoredPlacementDescriptor = 8;
/** Element class of a placement-list entry. */
inline constexpr std::uint32_t kAuthoredPlacementClass = 0x808099D8U;
/** One authored placement-list entry is 144 bytes. */
inline constexpr std::size_t kAuthoredPlacementStride = 144;
/** The entry names its placed-object class list here. */
inline constexpr std::size_t kAuthoredPlacementClassListOffset = 0;
/** Quaternion and world-position offsets inside one entry. */
inline constexpr std::size_t kAuthoredPlacementRotationOffset = 16;
inline constexpr std::size_t kAuthoredPlacementPositionOffset = 32;
inline constexpr std::size_t kAuthoredPlacementScaleOffset = 0x2C;
inline constexpr std::size_t kAuthoredPlacementNameHashOffset = 0x64;
inline constexpr std::size_t kAuthoredPlacementFlagsOffset = 0x68;
/** Bit 0 of the flags word forbids replication whatever the class definition says. */
inline constexpr std::uint32_t kAuthoredPlacementNoReplicationBit = 0x1U;
/** Opaque 64-bit identifier carried by one placement entry. */
inline constexpr std::size_t kAuthoredPlacementIdentifierOffset = 0x70;
/** One entry's optional self-relative auxiliary payload begins here. */
inline constexpr std::size_t kAuthoredPlacementAuxiliaryOffset = 0x78;
/** Marker carried by the proved auxiliary payload that names a spatial parent. */
inline constexpr std::uint32_t kStaticSpatialAuxiliaryClass = 0x808071B3U;
/** The auxiliary payload names its parent tag here. */
inline constexpr std::size_t kStaticSpatialAuxiliaryParentOffset = 16;
/** The proved parent record class and its candidate-table tag field. */
inline constexpr std::uint32_t kStaticSpatialParentClass = 0x80806EF4U;
inline constexpr std::size_t kStaticSpatialParentTableOffset = 8;
/** A bare indirect leaf names its resource here. */
inline constexpr std::size_t kAuthoredPlacementDirectTagOffset = 8;

/** One package-authored transform. It does not identify a live ClientRef. */
struct AuthoredPlacement final {
    std::uint32_t classListTag{};
    std::array<float, 4> rotation{};
    std::array<float, 3> position{};
    std::size_t sourceOffset{};
    std::array<std::uint32_t, 4> rotationBits{};
    std::array<std::uint32_t, 3> positionBits{};
    float uniformScale{};
    std::uint32_t uniformScaleBits{};
    std::uint32_t nameHash{};
    std::uint32_t placementFlagsRaw{};
    std::uint64_t placementIdentifier{};
    std::int64_t auxiliaryRelative{};
};

/** Finds and validates an authored placement array. */
[[nodiscard]] bool authored_placements(std::span<const std::byte> blob, Array& output) noexcept;

/** Reads one transform from a validated authored placement array. */
[[nodiscard]] bool authored_placement_at(std::span<const std::byte> blob,
                                         const Array& array,
                                         std::size_t index,
                                         AuthoredPlacement& output) noexcept;

/** Reads the opaque identifier at entry +0x70 without assigning it semantics. */
[[nodiscard]] bool authored_placement_identifier_at(std::span<const std::byte> blob,
                                                    const Array& array,
                                                    std::size_t index,
                                                    std::uint64_t& output) noexcept;

/** Reads the direct resource tag from the bare form of an indirect placement leaf. */
[[nodiscard]] bool authored_placement_direct_tag(std::span<const std::byte> blob,
                                                 std::uint32_t leafClass,
                                                 std::uint32_t& tag) noexcept;

/** Reads the parent tag from one proved self-relative auxiliary payload. */
[[nodiscard]] bool authored_placement_auxiliary_parent(std::span<const std::byte> blob,
                                                       const Array& array,
                                                       std::size_t index,
                                                       std::uint32_t& tag) noexcept;

/** Reads the candidate-table tag from one exact parent-class record. */
[[nodiscard]] bool static_spatial_parent_table(std::span<const std::byte> blob,
                                               std::uint32_t parentClass,
                                               std::uint32_t& tag) noexcept;

} // namespace sunrise::middleware::content::packages::tables
