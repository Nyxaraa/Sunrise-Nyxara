#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "definition_index_table.h"

namespace sunrise::middleware::content::packages::tables {

/** Tag class of the package table whose three arrays form one spatial candidate set. */
inline constexpr std::uint32_t kStaticSpatialTableClass = 0x8080966DU;
/** Tag class of the paired bounds-candidate table. */
inline constexpr std::uint32_t kStaticSpatialBoundsTableClass = 0x80809671U;
/** Element classes carried by the four proved array headers. */
inline constexpr std::uint32_t kStaticSpatialTransformClass = 0x808071A3U;
inline constexpr std::uint32_t kStaticSpatialResourceClass = 0x8080967DU;
inline constexpr std::uint32_t kStaticSpatialRangeClass = 0x80807190U;
inline constexpr std::uint32_t kStaticSpatialBoundsClass = 0x80809673U;
/** Array and link offsets inside the two package records. */
inline constexpr std::size_t kStaticSpatialBoundsTagOffset = 0x18;
inline constexpr std::size_t kStaticSpatialTransformDescriptor = 0x40;
inline constexpr std::size_t kStaticSpatialResourceDescriptor = 0x58;
inline constexpr std::size_t kStaticSpatialRangeDescriptor = 0x68;
inline constexpr std::size_t kStaticSpatialBoundsDescriptor = 0x08;
/** Row strides. Their engine meanings are still candidates. */
inline constexpr std::size_t kStaticSpatialTransformStride = 48;
inline constexpr std::size_t kStaticSpatialResourceStride = 4;
inline constexpr std::size_t kStaticSpatialRangeStride = 8;
inline constexpr std::size_t kStaticSpatialBoundsStride = 48;

/** Validated arrays and paired tag from one class-0x8080966D record. */
struct StaticSpatialTable final {
    std::uint32_t boundsTag{};
    Array transforms{};
    Array resources{};
    Array ranges{};
};

/** Three vec4-shaped fields retained without assigning native transform semantics. */
struct StaticSpatialTransformCandidate final {
    std::array<float, 4> first{};
    std::array<float, 4> second{};
    std::array<float, 4> third{};
};

/** One ordered eight-byte range row. */
struct StaticSpatialRange final {
    std::uint16_t count{};
    std::uint16_t first{};
    std::uint16_t resourceIndex{};
    std::uint16_t trailing{};
};

/** Two vec4-shaped fields and the still-opaque tail of one paired row. */
struct StaticSpatialBoundsCandidate final {
    std::array<float, 4> first{};
    std::array<float, 4> second{};
    std::array<std::byte, 16> opaque{};
};

/** Reads the three typed arrays and paired tag without assigning renderer semantics. */
[[nodiscard]] bool static_spatial_table(std::span<const std::byte> blob,
                                        StaticSpatialTable& output) noexcept;

/** Reads one three-vector row from a validated candidate table. */
[[nodiscard]] bool static_spatial_transform_at(std::span<const std::byte> blob,
                                               const Array& transforms,
                                               std::size_t index,
                                               StaticSpatialTransformCandidate& output) noexcept;

/** Reads one resource tag from a validated candidate table. */
[[nodiscard]] bool static_spatial_resource_at(std::span<const std::byte> blob,
                                              const Array& resources,
                                              std::size_t index,
                                              std::uint32_t& output) noexcept;

/** Reads one ordered range row from a validated candidate table. */
[[nodiscard]] bool static_spatial_range_at(std::span<const std::byte> blob,
                                           const Array& ranges,
                                           std::size_t index,
                                           StaticSpatialRange& output) noexcept;

/** Finds the typed row array in one class-0x80809671 record. */
[[nodiscard]] bool static_spatial_bounds(std::span<const std::byte> blob, Array& output) noexcept;

/** Reads one two-vector row and opaque tail from a validated paired table. */
[[nodiscard]] bool static_spatial_bounds_at(std::span<const std::byte> blob,
                                            const Array& bounds,
                                            std::size_t index,
                                            StaticSpatialBoundsCandidate& output) noexcept;

} // namespace sunrise::middleware::content::packages::tables
