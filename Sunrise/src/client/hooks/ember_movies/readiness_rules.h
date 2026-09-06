#pragma once
#include <cstdint>
namespace sunrise::client::hooks::ember_movies {
constexpr bool movie_resources_ready(int rootState,std::uint32_t asset,bool wrapperResident,
    std::uint32_t header,bool headerResident,std::uint32_t media) noexcept {
    return rootState==2 && (asset==0x80BCA001U || asset==0x80BCA003U)
        && wrapperResident && header==asset-1 && headerResident && media!=0xFFFFFFFFU;
}
constexpr bool resource_can_release(int state) noexcept { return state==2 || state==3; }
}
