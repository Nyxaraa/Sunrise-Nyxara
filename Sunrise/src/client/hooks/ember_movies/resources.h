#pragma once
#include <cstdint>
namespace sunrise::client::hooks::ember_movies {
bool sunburn_resident() noexcept;
// Load definitions before the callbacks which dereference them. Release the
// dependent containers/buffers before releasing the definitions they use.
class MovieResource {
    std::uint32_t root_{0xFFFFFFFFU}, surfaces_{0xFFFFFFFFU}, asset_{};
    bool retiringSurfaces_{};
public:
    bool begin(std::uint32_t asset) noexcept;
    bool advance() noexcept; // nonblocking: start dependent load only after definitions settle
    int state() const noexcept; // native root: 1 pending, 2 ready, 3 failed
    bool ready() const noexcept;
    bool prepare_surfaces() noexcept; // frame-owned publication after the native player is idle
    bool release() noexcept; // false while pending: never block the frame draining I/O
    bool held() const noexcept { return root_ != 0xFFFFFFFFU; }
};
}
