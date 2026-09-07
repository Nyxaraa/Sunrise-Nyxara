#pragma once
#include "config.h"
namespace sunrise::state::activity::presentation {
constexpr bool valid_tag(std::uint32_t tag) noexcept {
    return tag && tag != 0xFFFFFFFFU && tag != 0x811C9DC5U;
}
constexpr bool valid_config(const Config& config) noexcept {
    if (config.movieCount > config.movies.size()) return false;
    for (unsigned i = 0; i < config.movieCount; ++i) {
        const auto& m = config.movies[i];
        if (!valid_tag(m.asset) || !valid_tag(m.header) || !valid_tag(m.subtitles)
            || !valid_tag(m.catalog) || !valid_tag(m.stream))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (config.movies[j].asset == m.asset) return false;
    }
    if (config.movieCount) {
        std::array<std::uint32_t, 18> tags{};
        for (unsigned i = 0; i < 6; ++i) {
            tags[i] = config.surfaces.definitions[i];
            tags[i + 6] = config.surfaces.buffers[i];
            tags[i + 12] = config.surfaces.containers[i];
        }
        for (unsigned i = 0; i < tags.size(); ++i) {
            if (!valid_tag(tags[i])) return false;
            for (unsigned j = 0; j < i; ++j)
                if (tags[i] == tags[j]) return false;
        }
    }
    return true;
}
} // namespace sunrise::state::activity::presentation
