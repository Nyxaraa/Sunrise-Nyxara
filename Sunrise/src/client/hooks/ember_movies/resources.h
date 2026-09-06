#pragma once
#include <cstdint>
namespace sunrise::client::hooks::ember_movies {
bool sunburn_resident() noexcept;
// Native root owns the complete dependency graph until playback has released it.
class MovieResource {
    std::uint32_t root_{0xFFFFFFFFU}, asset_{};
public:
    bool begin(std::uint32_t asset) noexcept;
    int state() const noexcept; // native root: 1 pending, 2 ready, 3 failed
    bool ready() const noexcept;
    bool release() noexcept; // false while pending: never block the frame draining I/O
    bool held() const noexcept { return root_ != 0xFFFFFFFFU; }
};
}
