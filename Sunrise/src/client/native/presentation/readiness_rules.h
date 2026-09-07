#pragma once
#include <array>
#include <cstdint>

#include "config.h"
namespace sunrise::client::native::presentation {
// Native 426920 selects 1 for ordinary tags, 2 only for type_info & F000 == 2000.
// Callers declare ordinary-tag movie dependencies.
constexpr std::uint32_t movie_resource_kind = 1;
// Stream datums hold size plus a packed package offset/patch id, not a CPU blob.
// Native 3591B0 initializes this mapping without allocating the whole media file.
constexpr bool
movie_stream_ready(std::uint32_t size, std::uint32_t typeInfo, std::uint64_t location) noexcept {
    return (size & 0xC0000000U) == 0xC0000000U && (size & 0x3FFFFFFFU) > 0
           && ((typeInfo >> 6) & 0x3FU) == 24 && (typeInfo & 0x30000U) == 0x10000U
           && location <= 0xFFFFFFFFULL && (location & 0xFFFFFF00ULL) != 0;
}
constexpr bool movie_resources_ready(int rootState,
                                     const Movie& movie,
                                     bool wrapperResident,
                                     std::uint32_t header,
                                     bool headerResident,
                                     std::uint32_t media) noexcept {
    return rootState == 2 && movie.asset != 0 && movie.asset != 0xFFFFFFFFU && wrapperResident
           && header == movie.header && header != 0 && header != 0xFFFFFFFFU && headerResident
           && media == movie.stream && media != 0 && media != 0xFFFFFFFFU;
}
constexpr bool resource_can_release(int state) noexcept {
    return state == 2 || state == 3;
}
} // namespace sunrise::client::native::presentation
