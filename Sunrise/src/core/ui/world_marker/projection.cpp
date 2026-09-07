#include "projection.h"

#include <cmath>

namespace sunrise::core::ui::world_marker {
namespace {

// Projection tolerances, and pi as the upper bound on a valid field of view.
constexpr float kDepthEpsilon = 0.001F;
constexpr float kLengthEpsilon = 0.000001F;
constexpr float kPi = 3.14159265358979323846F;

/** @return Dot product of two three-lane vectors. */
[[nodiscard]] float dot(const Vector3& left, const Vector3& right) noexcept {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

/** @return The right-handed cross product. */
[[nodiscard]] Vector3 cross(const Vector3& left, const Vector3& right) noexcept {
    return {left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]};
}

/** Normalizes one finite vector. */
[[nodiscard]] bool normalize(Vector3& value) noexcept {
    const float lengthSquared = dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kLengthEpsilon) {
        return false;
    }
    const float inverse = 1.0F / std::sqrt(lengthSquared);
    for (float& lane : value) {
        lane *= inverse;
    }
    return true;
}

/** @return True when every lane is finite. */
[[nodiscard]] bool finite(const Vector3& value) noexcept {
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

} // namespace

/** Projects one world point into viewport pixels. */
ProjectionStatus project(const Vector3& point,
                         const Camera& camera,
                         const Viewport& viewport,
                         bool invertX,
                         bool invertY,
                         ScreenPoint& output) noexcept {
    output = {};
    if (!finite(point) || !finite(camera.position) || !finite(camera.forward) || !finite(camera.up)
        || !std::isfinite(camera.horizontalFov) || !std::isfinite(camera.aspect)
        || !std::isfinite(viewport.x) || !std::isfinite(viewport.y)
        || !std::isfinite(viewport.width) || !std::isfinite(viewport.height)
        || camera.horizontalFov <= 0.0F || camera.horizontalFov >= kPi || camera.aspect <= 0.0F
        || viewport.width <= 0.0F || viewport.height <= 0.0F) {
        return ProjectionStatus::invalid;
    }

    Vector3 forward = camera.forward;
    Vector3 up = camera.up;
    if (!normalize(forward)) {
        return ProjectionStatus::invalid;
    }
    const float upAlongForward = dot(up, forward);
    for (std::size_t lane = 0; lane < up.size(); ++lane) {
        up[lane] -= forward[lane] * upAlongForward;
    }
    if (!normalize(up)) {
        return ProjectionStatus::invalid;
    }
    Vector3 right = cross(forward, up);
    if (!normalize(right)) {
        return ProjectionStatus::invalid;
    }

    Vector3 relative{};
    for (std::size_t lane = 0; lane < relative.size(); ++lane) {
        relative[lane] = point[lane] - camera.position[lane];
    }
    const float depth = dot(relative, forward);
    if (!std::isfinite(depth) || depth <= kDepthEpsilon) {
        return ProjectionStatus::behind;
    }
    const float horizontalScale = std::tan(camera.horizontalFov * 0.5F);
    const float verticalScale = horizontalScale / camera.aspect;
    if (!std::isfinite(horizontalScale) || !std::isfinite(verticalScale) || horizontalScale <= 0.0F
        || verticalScale <= 0.0F) {
        return ProjectionStatus::invalid;
    }

    float horizontal = dot(relative, right) / (depth * horizontalScale);
    float vertical = dot(relative, up) / (depth * verticalScale);
    horizontal = invertX ? -horizontal : horizontal;
    vertical = invertY ? -vertical : vertical;
    if (!std::isfinite(horizontal) || !std::isfinite(vertical)) {
        return ProjectionStatus::invalid;
    }
    if (horizontal < -1.0F || horizontal > 1.0F || vertical < -1.0F || vertical > 1.0F) {
        return ProjectionStatus::outside;
    }

    output.x = viewport.x + (horizontal + 1.0F) * 0.5F * viewport.width;
    output.y = viewport.y + (1.0F - vertical) * 0.5F * viewport.height;
    return ProjectionStatus::visible;
}

} // namespace sunrise::core::ui::world_marker
