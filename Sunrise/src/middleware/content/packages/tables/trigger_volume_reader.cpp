#include "trigger_volume_reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "internal.h"

namespace sunrise::middleware::content::packages::tables {
namespace {

/** Field offsets of the installed trigger-volume component, root and row layouts. */
constexpr std::size_t kComponentHeaderOwnTagOffset = 0;
constexpr std::size_t kComponentHeaderClassOffset = 4;
constexpr std::size_t kComponentHeaderRootOffset = 8;
constexpr std::size_t kRootKeyOffset = 0x18;
constexpr std::size_t kRootIdentityOffset = 0x1C;
constexpr std::size_t kRootRowsDescriptor = 0x38;
constexpr std::size_t kRowKeyOffset = 0x0C;
constexpr std::size_t kRowIdentityOffset = 0x10;
constexpr std::size_t kRowFlagsOffset = 0x14;
constexpr std::size_t kRowSpawnEntryOffset = 0x20;
constexpr std::size_t kSpawnClassDefinitionOffset = 0;
constexpr std::size_t kSpawnRotationOffset = 0x10;
constexpr std::size_t kSpawnPositionOffset = 0x20;
constexpr std::size_t kSpawnAuxiliaryOffset = 0x78;
constexpr std::size_t kRowMinimumOffset = 0xB0;
constexpr std::size_t kRowMaximumOffset = 0xC0;
constexpr std::size_t kRowVertexDescriptor = 0xD0;
constexpr std::size_t kRowTriangleDescriptor = 0xE0;
constexpr std::size_t kRowExtrusionOffset = 0xF0;
constexpr std::size_t kRowActiveOffset = 0x11C;
/** An array header sits before its data, with the marker four bytes back. */
constexpr std::size_t kArrayMarkerBack = 4;
constexpr std::size_t kArrayClassOffset = 8;
constexpr std::size_t kArrayDataOffset = 0x10;
/** Auxiliary links a walk follows before it treats the chain as a loop. */
constexpr std::size_t kAuxiliaryChainCapacity = 32;
/** Field offsets of the installed shape-reference layout. */
constexpr std::size_t kShapeResourceOffset = 0x10;
constexpr std::size_t kShapeReferenceWordOffset = 0x14;
constexpr std::size_t kShapeIndexOffset = 0x18;
constexpr std::size_t kShapeKeyOffset = 0x1C;
constexpr std::size_t kShapeIdentityOffset = 0x20;

/** Reads a float4 without imposing alignment on package bytes. */
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

/** Adds one signed self-relative offset without overflowing host size arithmetic. */
[[nodiscard]] bool
relative_offset(std::size_t base, std::int64_t relative, std::size_t& output) noexcept {
    /** Signed range the addition must stay inside. */
    constexpr std::int64_t kMaximum = (std::numeric_limits<std::int64_t>::max)();
    constexpr std::int64_t kMinimum = (std::numeric_limits<std::int64_t>::min)();
    if (base > static_cast<std::size_t>(kMaximum)) {
        return false;
    }
    const std::int64_t signedBase = static_cast<std::int64_t>(base);
    if ((relative > 0 && signedBase > kMaximum - relative)
        || (relative < 0 && signedBase < kMinimum - relative)) {
        return false;
    }
    const std::int64_t result = signedBase + relative;
    if (result < 0) {
        return false;
    }
    output = static_cast<std::size_t>(result);
    return true;
}

/** Decodes the packed `{u8 type, u8 zero, u16 index}` identity. */
[[nodiscard]] bool read_identity(std::span<const std::byte> blob,
                                 std::size_t offset,
                                 TriggerVolumeIdentity& output) noexcept {
    output = {};
    std::uint8_t padding = 0;
    return read(blob, offset, output.type) && output.type != 0 && read(blob, offset + 1, padding)
           && padding == 0 && read(blob, offset + 2, output.index);
}

/** Resolves one non-empty typed array descriptor used inside an authored volume row. */
[[nodiscard]] bool row_array(std::span<const std::byte> blob,
                             std::size_t descriptor,
                             std::size_t capacity,
                             std::uint32_t elementClass,
                             std::size_t stride,
                             Array& output) noexcept {
    output = {};
    std::int64_t count = 0;
    std::int64_t relative = 0;
    std::size_t header = 0;
    std::uint32_t marker = 0;
    std::int64_t repeated = 0;
    std::uint32_t actualClass = 0;
    if (!read(blob, descriptor, count) || count <= 0 || count > static_cast<std::int64_t>(capacity)
        || !read(blob, descriptor + 8, relative)
        || !relative_offset(descriptor + 8, relative, header) || header < kArrayMarkerBack
        || !read(blob, header - kArrayMarkerBack, marker) || marker != kTriggerVolumeArrayMarker
        || !read(blob, header, repeated) || repeated != count
        || !read(blob, header + kArrayClassOffset, actualClass) || actualClass != elementClass
        || header > blob.size() || kArrayDataOffset > blob.size() - header) {
        return false;
    }
    const std::size_t data = header + kArrayDataOffset;
    const std::size_t itemCount = static_cast<std::size_t>(count);
    if (itemCount > (std::numeric_limits<std::size_t>::max)() / stride
        || itemCount * stride > blob.size() - data) {
        return false;
    }
    output = {static_cast<std::uint64_t>(count), data, actualClass};
    return true;
}

/** Follows the bounded SpawnEntry auxiliary chain to its exact native shape reference. */
[[nodiscard]] bool shape_reference(std::span<const std::byte> blob,
                                   std::size_t row,
                                   TriggerVolumeShapeReference& output) noexcept {
    output = {};
    const std::size_t field = row + kRowSpawnEntryOffset + kSpawnAuxiliaryOffset;
    std::int64_t relative = 0;
    std::size_t payload = 0;
    if (!read(blob, field, relative) || relative == 0
        || !relative_offset(field, relative, payload)) {
        return false;
    }
    std::array<std::size_t, kAuxiliaryChainCapacity> visited{};
    std::size_t visitedCount = 0;
    while (visitedCount < visited.size()) {
        const auto scanned = static_cast<std::ptrdiff_t>(visitedCount);
        if (payload < kArrayMarkerBack
            || std::find(visited.begin(), visited.begin() + scanned, payload)
                   != visited.begin() + scanned) {
            return false;
        }
        visited[visitedCount++] = payload;
        std::uint32_t marker = 0;
        if (!read(blob, payload - kArrayMarkerBack, marker) || (marker >> 16U) != 0x8080U) {
            return false;
        }
        if (marker == kTriggerVolumeShapeReferenceClass) {
            std::uint32_t key = 0;
            if (!read(blob, payload + kShapeResourceOffset, output.resourceTag)
                || package_of(output.resourceTag) == kAbsentPackageId
                || !read(blob, payload + kShapeReferenceWordOffset, output.referenceWord)
                || !read(blob, payload + kShapeIndexOffset, output.shapeIndex)
                || !read(blob, payload + kShapeKeyOffset, key)
                || !read_identity(blob, payload + kShapeIdentityOffset, output.identity)) {
                output = {};
                return false;
            }
            output.identity.key = key;
            return true;
        }
        if (!read(blob, payload, relative) || relative == 0
            || !relative_offset(payload, relative, payload)) {
            return false;
        }
    }
    return false;
}

/** @return True when all four lanes of one package vector are finite. */
[[nodiscard]] bool finite(const std::array<float, 4>& value) noexcept {
    return std::all_of(value.begin(), value.end(), [](float lane) { return std::isfinite(lane); });
}

} // namespace

/** Resolves one exact 0x808099C8 component row to its typed root. */
bool trigger_volume_root_at(std::span<const std::byte> blob,
                            const Array& components,
                            std::size_t componentIndex,
                            std::uint32_t configTag,
                            TriggerVolumeRoot& output) noexcept {
    output = {};
    std::size_t descriptor = 0;
    if (components.elementClass != kPlacedConfigComponentRowClass
        || !element_offset(components.dataOffset,
                           components.count,
                           kPlacedConfigComponentStride,
                           componentIndex,
                           descriptor)) {
        return false;
    }
    std::int32_t headerRelative = 0;
    std::uint32_t componentClass = 0;
    std::size_t header = 0;
    std::uint32_t ownTag = 0;
    std::uint32_t headerClass = 0;
    std::uint64_t rootOffset = 0;
    if (!read(blob, descriptor, headerRelative) || !read(blob, descriptor + 8, componentClass)
        || componentClass != kTriggerVolumeComponentClass
        || !relative_offset(descriptor, headerRelative, header)
        || !read(blob, header + kComponentHeaderOwnTagOffset, ownTag) || ownTag != configTag
        || !read(blob, header + kComponentHeaderClassOffset, headerClass)
        || headerClass != kTriggerVolumeComponentHeaderClass
        || !read(blob, header + kComponentHeaderRootOffset, rootOffset)
        || rootOffset > blob.size()) {
        return false;
    }
    const auto root = static_cast<std::size_t>(rootOffset);
    std::int64_t rowCount = 0;
    std::int64_t rowRelative = 0;
    std::uint32_t key = 0;
    if (!read(blob, root + kRootKeyOffset, key)
        || !read_identity(blob, root + kRootIdentityOffset, output.identity)
        || !read(blob, root + kRootRowsDescriptor, rowCount) || rowCount < 0
        || rowCount > static_cast<std::int64_t>(kTriggerVolumeRowCapacity)
        || !read(blob, root + kRootRowsDescriptor + 8, rowRelative)) {
        output = {};
        return false;
    }
    output.identity.key = key;
    output.rootOffset = static_cast<std::uint32_t>(rootOffset);
    output.componentOrdinal = static_cast<std::uint32_t>(componentIndex);
    if (rowCount == 0) {
        output.rows.elementClass = kTriggerVolumeRowClass;
        return rowRelative == 0;
    }
    std::size_t headerOffset = 0;
    std::uint32_t marker = 0;
    std::int64_t repeated = 0;
    std::uint32_t rowClass = 0;
    if (!relative_offset(root + kRootRowsDescriptor + 8, rowRelative, headerOffset)
        || headerOffset < kArrayMarkerBack || !read(blob, headerOffset - kArrayMarkerBack, marker)
        || marker != kTriggerVolumeArrayMarker || !read(blob, headerOffset, repeated)
        || repeated != rowCount || !read(blob, headerOffset + kArrayClassOffset, rowClass)
        || rowClass != kTriggerVolumeRowClass || headerOffset > blob.size()
        || kArrayDataOffset > blob.size() - headerOffset) {
        output = {};
        return false;
    }
    const std::size_t data = headerOffset + kArrayDataOffset;
    const std::size_t count = static_cast<std::size_t>(rowCount);
    if (count > (std::numeric_limits<std::size_t>::max)() / kTriggerVolumeRowStride
        || count * kTriggerVolumeRowStride > blob.size() - data) {
        output = {};
        return false;
    }
    output.rows = {static_cast<std::uint64_t>(rowCount), data, rowClass};
    return true;
}

/** Reads only the exact key/type/index identity from one fixed-stride authored row. */
bool trigger_volume_row_identity_at(std::span<const std::byte> blob,
                                    const TriggerVolumeRoot& root,
                                    std::size_t index,
                                    TriggerVolumeIdentity& output) noexcept {
    output = {};
    std::size_t row = 0;
    std::uint32_t key = 0;
    if (root.rows.elementClass != kTriggerVolumeRowClass
        || !element_offset(
            root.rows.dataOffset, root.rows.count, kTriggerVolumeRowStride, index, row)
        || !read(blob, row + kRowKeyOffset, key)
        || !read_identity(blob, row + kRowIdentityOffset, output)) {
        output = {};
        return false;
    }
    output.key = key;
    return true;
}

/** Reads one fixed-stride authored row and its required 0x8080929B shape reference. */
bool trigger_volume_row_at(std::span<const std::byte> blob,
                           const TriggerVolumeRoot& root,
                           std::size_t index,
                           TriggerVolumeRow& output) noexcept {
    output = {};
    std::size_t row = 0;
    std::uint32_t key = 0;
    if (root.rows.elementClass != kTriggerVolumeRowClass
        || !element_offset(
            root.rows.dataOffset, root.rows.count, kTriggerVolumeRowStride, index, row)
        || !read(blob, row + kRowKeyOffset, key)
        || !read_identity(blob, row + kRowIdentityOffset, output.identity)
        || !read(blob, row + kRowFlagsOffset, output.flags)
        || !read(blob,
                 row + kRowSpawnEntryOffset + kSpawnClassDefinitionOffset,
                 output.classDefinitionTag)
        || package_of(output.classDefinitionTag) == kAbsentPackageId
        || !read_vector(blob, row + kRowSpawnEntryOffset + kSpawnRotationOffset, output.rotation)
        || !read_vector(blob, row + kRowSpawnEntryOffset + kSpawnPositionOffset, output.position)
        || !read_vector(blob, row + kRowMinimumOffset, output.minimum)
        || !read_vector(blob, row + kRowMaximumOffset, output.maximum)
        || !row_array(blob,
                      row + kRowVertexDescriptor,
                      kTriggerVolumeVertexCapacity,
                      kTriggerVolumeVertexClass,
                      kTriggerVolumeVertexStride,
                      output.vertices)
        || !row_array(blob,
                      row + kRowTriangleDescriptor,
                      kTriggerVolumeTriangleCapacity,
                      kTriggerVolumeTriangleClass,
                      kTriggerVolumeTriangleStride,
                      output.triangles)
        || !read(blob, row + kRowExtrusionOffset, output.extrusion)
        || !read(blob, row + kRowActiveOffset, output.active)
        || !shape_reference(blob, row, output.shape)) {
        output = {};
        return false;
    }
    output.identity.key = key;
    return trigger_volume_row_finite(output);
}

/** Reads one world-coordinate float4 vertex from a validated row array. */
bool trigger_volume_vertex_at(std::span<const std::byte> blob,
                              const Array& vertices,
                              std::size_t index,
                              std::array<float, 4>& output) noexcept {
    output = {};
    std::size_t offset = 0;
    return vertices.elementClass == kTriggerVolumeVertexClass
           && element_offset(
               vertices.dataOffset, vertices.count, kTriggerVolumeVertexStride, index, offset)
           && read_vector(blob, offset, output) && finite(output);
}

/** Reads one tightly packed three-byte triangle from a validated row array. */
bool trigger_volume_triangle_at(std::span<const std::byte> blob,
                                const Array& triangles,
                                std::size_t index,
                                std::array<std::uint8_t, 3>& output) noexcept {
    output = {};
    std::size_t offset = 0;
    if (triangles.elementClass != kTriggerVolumeTriangleClass
        || !element_offset(
            triangles.dataOffset, triangles.count, kTriggerVolumeTriangleStride, index, offset)
        || offset > blob.size() || output.size() > blob.size() - offset) {
        return false;
    }
    std::memcpy(output.data(), blob.data() + offset, output.size());
    return true;
}

/** @return True only when every lane used by volume render math is finite and ordered. */
bool trigger_volume_row_finite(const TriggerVolumeRow& row) noexcept {
    if (!finite(row.rotation) || !finite(row.position) || !finite(row.minimum)
        || !finite(row.maximum) || !std::isfinite(row.extrusion) || row.extrusion < 0.0F) {
        return false;
    }
    for (std::size_t lane = 0; lane < 3; ++lane) {
        if (row.minimum[lane] > row.maximum[lane]) {
            return false;
        }
    }
    return true;
}

} // namespace sunrise::middleware::content::packages::tables
