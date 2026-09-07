#include "descriptor_embedded_placement_reader.h"

#include <cmath>
#include <limits>

#include "authored_placement_reader.h"
#include "definition_index_table.h"
#include "internal.h"

namespace sunrise::middleware::content::packages::tables {
namespace {

/** Entry fields not named by the shared authored-placement reader. */
constexpr std::size_t kNameHashOffset = 0x64;
constexpr std::size_t kReplicationByteOffset = 0x68;
constexpr std::size_t kGameworldByteOffset = 0x69;

/** Adds one signed self-relative field without wrapping host offsets. */
[[nodiscard]] bool
relative_offset(std::size_t base, std::int64_t relative, std::size_t& output) noexcept {
    /** Signed bounds keep the self-relative addition defined. */
    constexpr std::int64_t kMaximum = (std::numeric_limits<std::int64_t>::max)();
    constexpr std::int64_t kMinimum = (std::numeric_limits<std::int64_t>::min)();
    if (base > static_cast<std::size_t>(kMaximum)) {
        return false;
    }
    const auto signedBase = static_cast<std::int64_t>(base);
    if ((relative > 0 && signedBase > kMaximum - relative)
        || (relative < 0 && signedBase < kMinimum - relative)) {
        return false;
    }
    const auto target = signedBase + relative;
    if (target < 0) {
        return false;
    }
    output = static_cast<std::size_t>(target);
    return true;
}

/** Reads a fixed float vector without imposing alignment on package bytes. */
template <std::size_t Size>
[[nodiscard]] bool read_vector(std::span<const std::byte> blob,
                               std::size_t offset,
                               std::array<float, Size>& output) noexcept {
    output = {};
    for (std::size_t lane = 0; lane < Size; ++lane) {
        if (!read(blob, offset + lane * sizeof(float), output[lane])
            || !std::isfinite(output[lane])) {
            output = {};
            return false;
        }
    }
    return true;
}

} // namespace

/** Reads the exact native array descriptor. The zero-count, zero-relative form is valid. */
bool descriptor_embedded_placements(std::span<const std::byte> blob,
                                    const SlotDescriptor& descriptor,
                                    DescriptorEmbeddedPlacementArray& output) noexcept {
    output = {};
    const auto base = static_cast<std::size_t>(descriptor.descriptorOffset);
    /** The whole descriptor must sit inside the blob before any field is read. */
    constexpr std::size_t kRequiredSize = kDescriptorSize;
    if (descriptor.componentClass != kDescriptorEmbeddedPlacementDescriptorClass
        || descriptor.slotType != kDescriptorEmbeddedPlacementSlotType || base > blob.size()
        || kRequiredSize > blob.size() - base) {
        return false;
    }

    std::uint32_t configTag = 0;
    std::uint32_t descriptorClass = 0;
    std::uint32_t descriptorMark = 0;
    std::uint32_t bubbleIndex = 0;
    std::uint32_t senseSchema = 0;
    std::uint32_t authSchema = 0;
    std::uint16_t slotType = 0;
    std::uint16_t slotIndex = 0;
    if (!read(blob, base + kDescriptorOwnTagOffset, configTag) || configTag != descriptor.configTag
        || package_of(configTag) == kAbsentPackageId
        || !read(blob, base + kDescriptorComponentClassOffset, descriptorClass)
        || descriptorClass != kDescriptorEmbeddedPlacementDescriptorClass
        || !read(blob, base + kDescriptorMarkOffset, descriptorMark)
        || descriptorMark != kDescriptorMark
        || !read(blob, base + kDescriptorSlotTypeOffset, slotType)
        || slotType != kDescriptorEmbeddedPlacementSlotType
        || !read(blob, base + kDescriptorSlotIndexOffset, slotIndex)
        || slotIndex != descriptor.slotIndex
        || !read(blob, base + kDescriptorBubbleIndexOffset, bubbleIndex)
        || bubbleIndex != descriptor.bubbleIndex
        || !read(blob, base + kDescriptorSenseSchemaOffset, senseSchema)
        || senseSchema != descriptor.senseSchema || !is_schema_id(senseSchema)
        || !read(blob, base + kDescriptorAuthSchemaOffset, authSchema)
        || authSchema != descriptor.authSchema || !is_schema_id(authSchema)) {
        return false;
    }

    std::uint64_t count = 0;
    std::int64_t relative = 0;
    const std::size_t arrayDescriptor = base + kDescriptorEmbeddedPlacementArrayOffset;
    if (!read(blob, arrayDescriptor, count) || !read(blob, arrayDescriptor + 8, relative)) {
        return false;
    }
    if (count == 0) {
        return relative == 0;
    }
    Array array{};
    if (!find_array_at(blob, arrayDescriptor, array)
        || array.elementClass != kDescriptorEmbeddedPlacementClass || array.count != count
        || array.dataOffset > blob.size()
        || count > (blob.size() - array.dataOffset) / kDescriptorEmbeddedPlacementStride) {
        return false;
    }
    output.count = array.count;
    output.dataOffset = array.dataOffset;
    output.elementClass = array.elementClass;
    return true;
}

/** Reads one exact row from a validated descriptor-embedded placement array. */
bool descriptor_embedded_placement_at(std::span<const std::byte> blob,
                                      const DescriptorEmbeddedPlacementArray& array,
                                      std::uint64_t index,
                                      DescriptorEmbeddedPlacement& output) noexcept {
    output = {};
    if (array.elementClass != kDescriptorEmbeddedPlacementClass || index >= array.count
        || array.dataOffset > blob.size()
        || index > ((std::numeric_limits<std::size_t>::max)() - array.dataOffset)
                       / kDescriptorEmbeddedPlacementStride) {
        return false;
    }
    const std::size_t row =
        array.dataOffset + static_cast<std::size_t>(index) * kDescriptorEmbeddedPlacementStride;
    if (row > blob.size() || kDescriptorEmbeddedPlacementStride > blob.size() - row) {
        return false;
    }
    if (!read(blob, row + kAuthoredPlacementClassListOffset, output.classListTag)
        || package_of(output.classListTag) == kAbsentPackageId
        || !read_vector(blob, row + kAuthoredPlacementRotationOffset, output.rotation)
        || !read_vector(blob, row + kAuthoredPlacementPositionOffset, output.position)
        || !read(blob,
                 row + kAuthoredPlacementPositionOffset + output.position.size() * sizeof(float),
                 output.fourthLane)
        || !std::isfinite(output.fourthLane) || !read(blob, row + kNameHashOffset, output.nameHash)
        || !read(blob, row + kReplicationByteOffset, output.replicationByte)
        || !read(blob, row + kGameworldByteOffset, output.gameworldByte)
        || !read(blob, row + kAuthoredPlacementIdentifierOffset, output.identifier)
        || !read(blob, row + kAuthoredPlacementAuxiliaryOffset, output.auxiliaryRelative)) {
        output = {};
        return false;
    }

    if (output.auxiliaryRelative == 0) {
        return true;
    }
    const std::size_t auxiliaryField = row + kAuthoredPlacementAuxiliaryOffset;
    if (!relative_offset(auxiliaryField, output.auxiliaryRelative, output.auxiliaryOffset)
        || output.auxiliaryOffset >= blob.size()) {
        output = {};
        return false;
    }
    output.hasAuxiliary = true;
    return true;
}

} // namespace sunrise::middleware::content::packages::tables
