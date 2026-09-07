#pragma once

#include <array>
#include <cstdint>

namespace sunrise::middleware::content::packages::tables::static_spatial_aabb {

using Vector4 = std::array<float, 4>;

/** Native quaternion norm gate encoded as the exact build-86657 float bits. */
inline constexpr std::uint32_t kQuaternionNormThresholdBits = 0x38D1B717U;

/** Four rows of one affine matrix consumed by row vectors. */
struct Matrix final {
    std::array<Vector4, 4> rows{};
};

/** One ordered local or world AABB. The fourth lanes are homogeneous one. */
struct Bounds final {
    Vector4 minimum{};
    Vector4 maximum{};
};

/** Builds the exact row-vector affine matrix from q.xyzw and ts.xyz/uniformScale. */
[[nodiscard]] bool placement_matrix(const Vector4& quaternion,
                                    const Vector4& translationScale,
                                    Matrix& output) noexcept;

/** Applies one proved placement matrix to all eight corners of an ordered local AABB. */
[[nodiscard]] bool transform(const Bounds& local, const Matrix& placement, Bounds& output) noexcept;

/** Builds the placement matrix and transforms one ordered local AABB in one call. */
[[nodiscard]] bool world_bounds(const Vector4& quaternion,
                                const Vector4& translationScale,
                                const Bounds& local,
                                Bounds& output) noexcept;

} // namespace sunrise::middleware::content::packages::tables::static_spatial_aabb
