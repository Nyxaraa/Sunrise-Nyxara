#pragma once
#include <cstdint>
#include <array>

namespace sunrise::state::activity::presentation {
struct Owner {
    std::uint64_t session{}, generation{};
    bool operator==(const Owner&) const = default;
};
struct Movie {
    std::uint32_t asset{}, header{}, subtitles{}, catalog{}, stream{};
    bool operator==(const Movie&) const = default;
};
struct SurfaceSet {
    std::array<std::uint32_t, 6> definitions{}, buffers{}, containers{};
    bool operator==(const SurfaceSet&) const = default;
};
struct Config {
    std::array<Movie, 8> movies{};
    SurfaceSet surfaces{};
    unsigned movieCount{};
    bool suppressLoadingCinematics{};
    bool maskLoadingScreen{};
};
} // namespace sunrise::state::activity::presentation
