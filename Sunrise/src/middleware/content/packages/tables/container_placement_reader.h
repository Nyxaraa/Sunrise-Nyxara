#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "authored_placement_reader.h"

namespace sunrise::middleware::content::packages::tables {

/** Installed class of a placed object definition. */
inline constexpr std::uint32_t kPlacedClassDefinitionClass = 0x80809C0FU;
/** Its ordered construction rows use this element class and fixed stride. */
inline constexpr std::uint32_t kPlacedClassBuildRowClass = 0x80809C04U;
inline constexpr std::size_t kPlacedClassBuildDescriptor = 0x10;
inline constexpr std::size_t kPlacedClassBuildStride = 12;
/** The class definition records its authored type byte here. */
inline constexpr std::size_t kPlacedClassObjectTypeOffset = 0x96;
/** Flag byte after the type; bit 0x10 is what the game's replication predicate reads. */
inline constexpr std::size_t kPlacedClassFlagsOffset = 0x98;
inline constexpr std::uint8_t kPlacedClassNetworkReplicatedBit = 0x10;
/** Installed class of one placed object config. */
inline constexpr std::uint32_t kPlacedConfigClass = 0x80809C36U;
/** Its component rows use this element class and four-word layout. */
inline constexpr std::uint32_t kPlacedConfigComponentRowClass = 0x808091A4U;
inline constexpr std::size_t kPlacedConfigComponentDescriptor = 0x30;
inline constexpr std::size_t kPlacedConfigComponentStride = 16;
/** Installed class of an object-behavior root. */
inline constexpr std::uint32_t kObjectBehaviorRootClass = 0x8080941EU;
/** The fourth lane beside world translation is the native uniform scale input. */
inline constexpr std::size_t kContainerPlacementUniformScaleOffset =
    kAuthoredPlacementPositionOffset + 3 * sizeof(float);

/** Parsed header fields of one placed class definition. */
struct PlacedClassDefinition final {
    Array builds{};
    std::uint8_t objectType{};
    /** True when objects of this class replicate over the network. */
    bool networkReplicated{};
};

/** One ordered class-construction row. */
struct PlacedClassBuildRow final {
    std::uint32_t configTag{};
    std::uint32_t secondWord{};
    std::uint32_t thirdWord{};
};

/** One exact four-word config component row. */
struct PlacedConfigComponentRow final {
    std::uint32_t firstWord{};
    std::uint32_t secondWord{};
    std::uint32_t componentClass{};
    std::uint32_t fourthWord{};
};

/** @return The established label for a binary-carried object-type value. */
[[nodiscard]] const char* placed_object_type_name(std::uint8_t type) noexcept;

/** Reads the exact uniform-scale lane without changing the legacy authored-placement record. */
[[nodiscard]] bool container_placement_uniform_scale_at(std::span<const std::byte> blob,
                                                        const Array& placements,
                                                        std::size_t index,
                                                        float& output) noexcept;

/** Reads the typed build array and authored type of one exact class definition. */
[[nodiscard]] bool placed_class_definition(std::span<const std::byte> blob,
                                           PlacedClassDefinition& output) noexcept;

/** Reads one ordered build row from a validated placed class definition. */
[[nodiscard]] bool placed_class_build_at(std::span<const std::byte> blob,
                                         const Array& builds,
                                         std::size_t index,
                                         PlacedClassBuildRow& output) noexcept;

/** Reads the typed component array from one exact placed config. */
[[nodiscard]] bool placed_config_components(std::span<const std::byte> blob,
                                            Array& output) noexcept;

/** Reads one four-word row from a validated config component array. */
[[nodiscard]] bool placed_config_component_at(std::span<const std::byte> blob,
                                              const Array& components,
                                              std::size_t index,
                                              PlacedConfigComponentRow& output) noexcept;

} // namespace sunrise::middleware::content::packages::tables
