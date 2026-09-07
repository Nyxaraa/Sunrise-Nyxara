#pragma once

#include <array>

namespace sunrise::core::ui::world_marker {

using Vector3 = std::array<float, 3>;

/** Read-only camera fields used to project one package-authored anchor. */
struct Camera final {
    Vector3 position{};
    Vector3 forward{};
    Vector3 up{};
    float horizontalFov{};
    float aspect{};
};

/** Pixel rectangle the point is projected into. */
struct Viewport final {
    float x{};
    float y{};
    float width{};
    float height{};
};

/** Pixel result inside a viewport. */
struct ScreenPoint final {
    float x{};
    float y{};
};

enum class ProjectionStatus {
    visible,
    behind,
    outside,
    invalid,
};

/** Projects one world point without reading or writing game-owned render state. */
[[nodiscard]] ProjectionStatus project(const Vector3& point,
                                       const Camera& camera,
                                       const Viewport& viewport,
                                       bool invertX,
                                       bool invertY,
                                       ScreenPoint& output) noexcept;

} // namespace sunrise::core::ui::world_marker
