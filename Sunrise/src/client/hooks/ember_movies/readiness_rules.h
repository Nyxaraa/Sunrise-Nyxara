#pragma once
#include <cstdint>
#include <array>
namespace sunrise::client::hooks::ember_movies {
// Native 426920 selects 1 for ordinary tags, 2 only for type_info & F000 == 2000.
// The four movie metadata records below are all ordinary tags (100A/103B/1019).
constexpr std::uint32_t movie_resource_kind = 1;
constexpr std::array<std::uint32_t,4> movie_metadata(std::uint32_t asset) noexcept {
    if (asset!=0x80BCA001U && asset!=0x80BCA003U) return {};
    return {asset,asset-1,asset==0x80BCA001U ? 0x80B9EB33U : 0x80B9EB34U,0x80BCA032U};
}
constexpr std::uint32_t movie_stream(std::uint32_t asset) noexcept {
    return asset==0x80BCA001U ? 0x80BCA034U : asset==0x80BCA003U ? 0x80C7C000U : 0xFFFFFFFFU;
}
// Stream datums hold size plus a packed package offset/patch id, not a CPU blob.
// Native 3591B0 initializes this mapping without allocating the whole media file.
constexpr bool movie_stream_ready(std::uint32_t size, std::uint32_t typeInfo,
                                   std::uint64_t location) noexcept {
    return (size&0xC0000000U)==0xC0000000U && (size&0x3FFFFFFFU)>0
        && ((typeInfo>>6)&0x3FU)==24 && (typeInfo&0x30000U)==0x10000U
        && location<=0xFFFFFFFFULL && (location&0xFFFFFF00ULL)!=0;
}
constexpr bool movie_resources_ready(int rootState,std::uint32_t asset,bool wrapperResident,
    std::uint32_t header,bool headerResident,std::uint32_t media) noexcept {
    return rootState==2 && (asset==0x80BCA001U || asset==0x80BCA003U)
        && wrapperResident && header==asset-1 && headerResident && media!=0xFFFFFFFFU;
}
constexpr bool resource_can_release(int state) noexcept { return state==2 || state==3; }
}
