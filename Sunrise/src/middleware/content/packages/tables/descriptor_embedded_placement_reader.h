#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "slot_descriptor_reader.h"

namespace sunrise::middleware::content::packages::tables {

/** Exact on-disk descriptor class and slot type for this family. */
inline constexpr std::uint32_t kDescriptorEmbeddedPlacementDescriptorClass = 0x80809927U;
inline constexpr std::uint16_t kDescriptorEmbeddedPlacementSlotType = 4;
/** The descriptor carries a native count/self-relative placed-object array here. */
inline constexpr std::size_t kDescriptorEmbeddedPlacementArrayOffset = 0x58;
inline constexpr std::uint32_t kDescriptorEmbeddedPlacementClass = 0x808099D8U;
inline constexpr std::size_t kDescriptorEmbeddedPlacementStride = 0x90;

/** One validated native array owned by an exact type-4 slot descriptor. */
struct DescriptorEmbeddedPlacementArray final {
    std::uint64_t count{};
    std::size_t dataOffset{};
    std::uint32_t elementClass{};
};

/** One exact placed-object row embedded in a validated slot descriptor. */
struct DescriptorEmbeddedPlacement final {
    std::uint32_t classListTag{};
    std::uint32_t nameHash{};
    std::uint64_t identifier{};
    std::int64_t auxiliaryRelative{};
    std::size_t auxiliaryOffset{};
    std::array<float, 4> rotation{};
    std::array<float, 3> position{};
    float fourthLane{};
    std::uint8_t replicationByte{};
    std::uint8_t gameworldByte{};
    bool hasAuxiliary{};
};

/** Reads the exact native array descriptor. The zero-count, zero-relative form is valid. */
[[nodiscard]] bool
descriptor_embedded_placements(std::span<const std::byte> blob,
                               const SlotDescriptor& descriptor,
                               DescriptorEmbeddedPlacementArray& output) noexcept;

/** Reads one exact row from a validated descriptor-embedded placement array. */
[[nodiscard]] bool descriptor_embedded_placement_at(std::span<const std::byte> blob,
                                                    const DescriptorEmbeddedPlacementArray& array,
                                                    std::uint64_t index,
                                                    DescriptorEmbeddedPlacement& output) noexcept;

} // namespace sunrise::middleware::content::packages::tables
