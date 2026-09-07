#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <span>

#include "../../teleport/runtime.h"

namespace sunrise::client::hooks::graphics::renderer::world_lines {

using Vector3 = std::array<float, 3>;

/** One RGBA colour stored in vertex-byte order. */
struct Color final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};
};

/** Three equal world-space lines crossing at one proved position. */
struct Point final {
    Vector3 centre{};
    float halfExtent{1.0F};
    Color color{};
};

/** Three positive world-space axes rooted at one proved position. */
struct Axes final {
    Vector3 origin{};
    float extent{1.0F};
    Color color{};
};

/** One ordered world-space box. The caller owns the meaning of its bounds. */
struct Box final {
    Vector3 minimum{};
    Vector3 maximum{};
    Color color{};
};

/** One world-space sphere volume rendered as three great circles. */
struct Sphere final {
    Vector3 centre{};
    float radius{1.0F};
    std::uint16_t segments{16};
    Color color{};
};

/** One explicit world-space edge, including an edge from proved mesh geometry. */
struct Edge final {
    Vector3 first{};
    Vector3 second{};
    Color color{};
};

/** Typed world primitives submitted in one bounded line-list draw. */
struct Batch final {
    std::span<const Point> points{};
    std::span<const Axes> axes{};
    std::span<const Box> boxes{};
    std::span<const Sphere> spheres{};
    std::span<const Edge> edges{};
    float lineWidthPixels{2.0F};
    bool invertX{};
    bool invertY{};
};

/** Result of one private depth-independent D3D11 line pass. */
struct Result final {
    std::size_t vertices{};
    bool rendered{};
    bool truncated{};
};

/** Draws one bounded world-line batch without reading or writing game render caches. */
[[nodiscard]] Result draw(ID3D11Device* device,
                          ID3D11DeviceContext* context,
                          ID3D11RenderTargetView* target,
                          const teleport::CameraPose& camera,
                          const Batch& batch) noexcept;

/** Releases every device-owned resource created by the world-line pass. */
void release() noexcept;

} // namespace sunrise::client::hooks::graphics::renderer::world_lines
