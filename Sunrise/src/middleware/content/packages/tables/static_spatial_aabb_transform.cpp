#include "static_spatial_aabb_transform.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>

#if defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#endif

namespace sunrise::middleware::content::packages::tables::static_spatial_aabb {
namespace {

/** Homogeneous AABB corners always use one rather than either stored fourth lane. */
constexpr float kHomogeneousOne = 1.0F;
/** Native quaternion normalization doubles the reciprocal squared norm. */
constexpr float kQuaternionNormalizationNumerator = 2.0F;
/** Three bits select the minimum or maximum source for each spatial lane. */
constexpr std::size_t kCornerCount = 8;

/** @return True when every float lane is finite. */
[[nodiscard]] bool finite(const Vector4& value) noexcept {
    return std::all_of(
        value.begin(), value.end(), [](float lane) noexcept { return std::isfinite(lane); });
}

/** @return True when every stored minimum lane is ordered before its maximum lane. */
[[nodiscard]] bool ordered(const Bounds& value) noexcept {
    for (std::size_t lane = 0; lane < value.minimum.size(); ++lane) {
        if (value.minimum[lane] > value.maximum[lane]) {
            return false;
        }
    }
    return true;
}

/** Multiplies one row vector with the native pairwise addition order. */
[[nodiscard]] Vector4 transform_point(const Vector4& point, const Matrix& matrix) noexcept {
    Vector4 output{};
    for (std::size_t lane = 0; lane < output.size(); ++lane) {
        const float translation = point[3] * matrix.rows[3][lane];
        const float z = point[2] * matrix.rows[2][lane];
        const float y = point[1] * matrix.rows[1][lane];
        const float x = point[0] * matrix.rows[0][lane];
        const float left = translation + z;
        const float right = y + x;
        output[lane] = left + right;
    }
    return output;
}

} // namespace

/** Builds the exact row-vector affine matrix from q.xyzw and ts.xyz/uniformScale. */
bool placement_matrix(const Vector4& quaternion,
                      const Vector4& translationScale,
                      Matrix& output) noexcept {
    output = {};
    if (!finite(quaternion) || !finite(translationScale)) {
        return false;
    }

    const float x = quaternion[0];
    const float y = quaternion[1];
    const float z = quaternion[2];
    const float w = quaternion[3];
    const float xSquared = x * x;
    const float zSquared = z * z;
    const float ySquared = y * y;
    const float wSquared = w * w;
    const float normLeft = xSquared + zSquared;
    const float normRight = ySquared + wSquared;
    const float normSquared = normLeft + normRight;
    const float threshold = std::bit_cast<float>(kQuaternionNormThresholdBits);
    const float reciprocal = normSquared > threshold ? 1.0F / normSquared : 0.0F;
    const float factor = kQuaternionNormalizationNumerator * reciprocal;

    const float scaledX = x * factor;
    const float scaledY = y * factor;
    const float scaledZ = z * factor;
    const float xx = x * scaledX;
    const float xy = x * scaledY;
    const float xz = x * scaledZ;
    const float yy = y * scaledY;
    const float yz = y * scaledZ;
    const float zz = z * scaledZ;
    const float wx = w * scaledX;
    const float wy = w * scaledY;
    const float wz = w * scaledZ;
    const float scale = translationScale[3];

    const float row0X = (1.0F - yy) - zz;
    const float row0Y = xy + wz;
    const float row0Z = xz - wy;
    const float row1X = xy - wz;
    const float row1Y = (1.0F - xx) - zz;
    const float row1Z = yz + wx;
    const float row2X = xz + wy;
    const float row2Y = yz - wx;
    const float row2Z = (1.0F - xx) - yy;
    output.rows[0] = {scale * row0X, scale * row0Y, scale * row0Z, 0.0F};
    output.rows[1] = {scale * row1X, scale * row1Y, scale * row1Z, 0.0F};
    output.rows[2] = {scale * row2X, scale * row2Y, scale * row2Z, 0.0F};
    output.rows[3] = {
        translationScale[0], translationScale[1], translationScale[2], kHomogeneousOne};
    return finite(output.rows[0]) && finite(output.rows[1]) && finite(output.rows[2])
           && finite(output.rows[3]);
}

/** Applies one proved placement matrix to all eight corners of an ordered local AABB. */
bool transform(const Bounds& local, const Matrix& placement, Bounds& output) noexcept {
    output = {};
    if (!finite(local.minimum) || !finite(local.maximum) || !ordered(local)) {
        return false;
    }
    for (const Vector4& row : placement.rows) {
        if (!finite(row)) {
            return false;
        }
    }

    for (std::size_t cornerIndex = 0; cornerIndex < kCornerCount; ++cornerIndex) {
        Vector4 corner{kHomogeneousOne, kHomogeneousOne, kHomogeneousOne, kHomogeneousOne};
        for (std::size_t lane = 0; lane < 3; ++lane) {
            const std::size_t select = std::size_t{1} << lane;
            corner[lane] = (cornerIndex & select) != 0 ? local.maximum[lane] : local.minimum[lane];
        }
        const Vector4 transformed = transform_point(corner, placement);
        if (!finite(transformed)) {
            output = {};
            return false;
        }
        if (cornerIndex == 0) {
            output.minimum = transformed;
            output.maximum = transformed;
            continue;
        }
        for (std::size_t lane = 0; lane < 3; ++lane) {
            output.minimum[lane] = (std::min)(output.minimum[lane], transformed[lane]);
            output.maximum[lane] = (std::max)(output.maximum[lane], transformed[lane]);
        }
    }
    output.minimum[3] = kHomogeneousOne;
    output.maximum[3] = kHomogeneousOne;
    return true;
}

/** Builds the placement matrix and transforms one ordered local AABB in one call. */
bool world_bounds(const Vector4& quaternion,
                  const Vector4& translationScale,
                  const Bounds& local,
                  Bounds& output) noexcept {
    Matrix placement{};
    if (!placement_matrix(quaternion, translationScale, placement)) {
        output = {};
        return false;
    }
    return transform(local, placement, output);
}

} // namespace sunrise::middleware::content::packages::tables::static_spatial_aabb

#if defined(_MSC_VER)
#pragma float_control(pop)
#endif
