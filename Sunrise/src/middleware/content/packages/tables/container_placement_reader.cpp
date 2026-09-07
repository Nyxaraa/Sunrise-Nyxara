#include "container_placement_reader.h"

#include <array>

#include "internal.h"

namespace sunrise::middleware::content::packages::tables {

/** @return The established label for a binary-carried object-type value. */
const char* placed_object_type_name(std::uint8_t type) noexcept {
    /** Labels in binary value order, so the value indexes the row directly. */
    constexpr std::array<const char*, 29> kNames{
        "inherited",
        "static_mesh",
        "prop_simple_DEPRECATED",
        "prop_expensive_DEPRECATED",
        "prop_cosmetic_static",
        "prop_cosmetic_movable",
        "prop_cosmetic_movable_garbage",
        "prop_networked_static",
        "prop_networked_movable",
        "prop_cinematic",
        "speedtree",
        "interactive",
        "biped",
        "creature",
        "weapon",
        "vehicle",
        "turret",
        "emitter",
        "projectile",
        "item",
        "item_ammo",
        "item_loot",
        "gear",
        "hop_on",
        "hop_on_gear_biped",
        "hop_on_gear_weapon",
        "hop_on_gear_ship",
        "hop_on_gear_sparrow",
        "system",
    };
    return type < kNames.size() ? kNames[type] : "unknown";
}

namespace {

/** Empty package arrays encode both descriptor words as zero. */
[[nodiscard]] bool empty_array_at(std::span<const std::byte> blob,
                                  std::size_t descriptor) noexcept {
    std::uint64_t count = 1;
    std::int64_t relative = 1;
    return read(blob, descriptor, count) && read(blob, descriptor + 8, relative) && count == 0
           && relative == 0;
}

} // namespace

/** Reads the exact uniform-scale lane without changing the legacy authored-placement record. */
bool container_placement_uniform_scale_at(std::span<const std::byte> blob,
                                          const Array& placements,
                                          std::size_t index,
                                          float& output) noexcept {
    output = 0.0F;
    if (placements.elementClass != kAuthoredPlacementClass) {
        return false;
    }
    std::size_t offset = 0;
    return element_offset(
               placements.dataOffset, placements.count, kAuthoredPlacementStride, index, offset)
           && read(blob, offset + kContainerPlacementUniformScaleOffset, output);
}

/** Reads the typed build array and authored type of one exact class definition. */
bool placed_class_definition(std::span<const std::byte> blob,
                             PlacedClassDefinition& output) noexcept {
    output = {};
    std::uint8_t flags = 0;
    if (!read(blob, kPlacedClassObjectTypeOffset, output.objectType)
        || !read(blob, kPlacedClassFlagsOffset, flags)) {
        return false;
    }
    output.networkReplicated = (flags & kPlacedClassNetworkReplicatedBit) != 0;
    if (empty_array_at(blob, kPlacedClassBuildDescriptor)) {
        return true;
    }
    if (!find_array_at(blob, kPlacedClassBuildDescriptor, output.builds)
        || output.builds.elementClass != kPlacedClassBuildRowClass) {
        output = {};
        return false;
    }
    return true;
}

/** Reads one ordered build row from a validated placed class definition. */
bool placed_class_build_at(std::span<const std::byte> blob,
                           const Array& builds,
                           std::size_t index,
                           PlacedClassBuildRow& output) noexcept {
    output = {};
    if (builds.elementClass != kPlacedClassBuildRowClass) {
        return false;
    }
    std::size_t offset = 0;
    return element_offset(builds.dataOffset, builds.count, kPlacedClassBuildStride, index, offset)
           && read(blob, offset, output.configTag) && read(blob, offset + 4, output.secondWord)
           && read(blob, offset + 8, output.thirdWord)
           && package_of(output.configTag) != kAbsentPackageId;
}

/** Reads the typed component array from one exact placed config. */
bool placed_config_components(std::span<const std::byte> blob, Array& output) noexcept {
    output = {};
    if (empty_array_at(blob, kPlacedConfigComponentDescriptor)) {
        return true;
    }
    if (!find_array_at(blob, kPlacedConfigComponentDescriptor, output)
        || output.elementClass != kPlacedConfigComponentRowClass) {
        output = {};
        return false;
    }
    return true;
}

/** Reads one four-word row from a validated config component array. */
bool placed_config_component_at(std::span<const std::byte> blob,
                                const Array& components,
                                std::size_t index,
                                PlacedConfigComponentRow& output) noexcept {
    output = {};
    if (components.elementClass != kPlacedConfigComponentRowClass) {
        return false;
    }
    std::size_t offset = 0;
    return element_offset(
               components.dataOffset, components.count, kPlacedConfigComponentStride, index, offset)
           && read(blob, offset, output.firstWord) && read(blob, offset + 4, output.secondWord)
           && read(blob, offset + 8, output.componentClass)
           && read(blob, offset + 12, output.fourthWord);
}

} // namespace sunrise::middleware::content::packages::tables
