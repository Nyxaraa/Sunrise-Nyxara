#include "static_spatial_candidate_reader.h"

#include <cstring>

#include "internal.h"

namespace sunrise::middleware::content::packages::tables {
namespace {

/** Reads one vec4 without imposing package-byte alignment. */
[[nodiscard]] bool read_vector(std::span<const std::byte> blob,
                               std::size_t offset,
                               std::array<float, 4>& output) noexcept {
    output = {};
    for (std::size_t lane = 0; lane < output.size(); ++lane) {
        if (!read(blob, offset + lane * sizeof(float), output[lane])) {
            output = {};
            return false;
        }
    }
    return true;
}

/** @return True when a found array names this element class, or holds no entries. */
[[nodiscard]] bool typed_array(const Array& array, std::uint32_t elementClass) noexcept {
    return array.count == 0 || array.elementClass == elementClass;
}

} // namespace

/** Reads the three typed arrays and paired tag without assigning renderer semantics. */
bool static_spatial_table(std::span<const std::byte> blob, StaticSpatialTable& output) noexcept {
    output = {};
    // Each array may be authored with no entries, and an empty one carries no element class.
    if (!read(blob, kStaticSpatialBoundsTagOffset, output.boundsTag)
        || package_of(output.boundsTag) == kAbsentPackageId
        || !find_optional_array_at(blob, kStaticSpatialTransformDescriptor, output.transforms)
        || !typed_array(output.transforms, kStaticSpatialTransformClass)
        || !find_optional_array_at(blob, kStaticSpatialResourceDescriptor, output.resources)
        || !typed_array(output.resources, kStaticSpatialResourceClass)
        || !find_optional_array_at(blob, kStaticSpatialRangeDescriptor, output.ranges)
        || !typed_array(output.ranges, kStaticSpatialRangeClass)) {
        output = {};
        return false;
    }
    return true;
}

/** Reads one three-vector row from a validated candidate table. */
bool static_spatial_transform_at(std::span<const std::byte> blob,
                                 const Array& transforms,
                                 std::size_t index,
                                 StaticSpatialTransformCandidate& output) noexcept {
    output = {};
    std::size_t offset = 0;
    if (transforms.elementClass != kStaticSpatialTransformClass
        || !element_offset(
            transforms.dataOffset, transforms.count, kStaticSpatialTransformStride, index, offset)
        || !read_vector(blob, offset, output.first)
        || !read_vector(blob, offset + 16, output.second)
        || !read_vector(blob, offset + 32, output.third)) {
        output = {};
        return false;
    }
    return true;
}

/** Reads one resource tag from a validated candidate table. */
bool static_spatial_resource_at(std::span<const std::byte> blob,
                                const Array& resources,
                                std::size_t index,
                                std::uint32_t& output) noexcept {
    output = 0;
    std::size_t offset = 0;
    return resources.elementClass == kStaticSpatialResourceClass
           && element_offset(
               resources.dataOffset, resources.count, kStaticSpatialResourceStride, index, offset)
           && read(blob, offset, output) && package_of(output) != kAbsentPackageId;
}

/** Reads one ordered range row from a validated candidate table. */
bool static_spatial_range_at(std::span<const std::byte> blob,
                             const Array& ranges,
                             std::size_t index,
                             StaticSpatialRange& output) noexcept {
    output = {};
    std::size_t offset = 0;
    if (ranges.elementClass != kStaticSpatialRangeClass
        || !element_offset(
            ranges.dataOffset, ranges.count, kStaticSpatialRangeStride, index, offset)
        || !read(blob, offset, output.count) || !read(blob, offset + 2, output.first)
        || !read(blob, offset + 4, output.resourceIndex)
        || !read(blob, offset + 6, output.trailing)) {
        output = {};
        return false;
    }
    return true;
}

/** Finds the typed row array in one class-0x80809671 record. */
bool static_spatial_bounds(std::span<const std::byte> blob, Array& output) noexcept {
    output = {};
    return find_optional_array_at(blob, kStaticSpatialBoundsDescriptor, output)
           && typed_array(output, kStaticSpatialBoundsClass);
}

/** Reads one two-vector row and opaque tail from a validated paired table. */
bool static_spatial_bounds_at(std::span<const std::byte> blob,
                              const Array& bounds,
                              std::size_t index,
                              StaticSpatialBoundsCandidate& output) noexcept {
    output = {};
    std::size_t offset = 0;
    if (bounds.elementClass != kStaticSpatialBoundsClass
        || !element_offset(
            bounds.dataOffset, bounds.count, kStaticSpatialBoundsStride, index, offset)
        || !read_vector(blob, offset, output.first)
        || !read_vector(blob, offset + 16, output.second) || offset + 32 > blob.size()
        || output.opaque.size() > blob.size() - (offset + 32)) {
        output = {};
        return false;
    }
    std::memcpy(output.opaque.data(), blob.data() + offset + 32, output.opaque.size());
    return true;
}

} // namespace sunrise::middleware::content::packages::tables
