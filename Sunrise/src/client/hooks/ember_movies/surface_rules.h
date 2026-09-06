#pragma once
#include <array>
#include <cstdint>
namespace sunrise::client::hooks::ember_movies {
// 80BCA032 names the six Y/U/V double-buffer surface containers. Loading that
// list alone does not retain/activate its individual surface containers.
constexpr std::array<std::uint32_t,6> movie_surfaces{
    0x80BCA022U,0x80BCA025U,0x80BCA028U,0x80BCA02BU,0x80BCA02EU,0x80BCA031U};
constexpr std::array<std::uint32_t,6> movie_surface_definitions{
    0x80BCA021U,0x80BCA024U,0x80BCA026U,0x80BCA029U,0x80BCA02CU,0x80BCA02FU};
struct SurfaceRegistration {
    std::uint32_t count{}, entries[3]{}, selected{0xFFFFFFFFU};
};
static_assert(sizeof(SurfaceRegistration)==20);
using SurfaceRegistrations=std::array<SurfaceRegistration,8>;
constexpr std::uint32_t selected_surface(const SurfaceRegistration& r) noexcept {
    return r.count>0 && r.count<=3 ? r.entries[r.count-1] : 0xFFFFFFFFU;
}
constexpr bool movie_surfaces_registered(const SurfaceRegistrations& rows) noexcept {
    for (unsigned i=0;i<rows.size();++i) {
        if (rows[i].count>3) return false;
        const auto top=selected_surface(rows[i]);
        // The native publisher updates all eight slots. Do not change unrelated
        // pending slot 0/7 selections while preparing an Ember movie.
        if (i==0 || i==7) { if (rows[i].selected!=top) return false; }
        else if (top!=movie_surface_definitions[i-1]) return false;
    }
    return true;
}
constexpr bool movie_surfaces_selected(const SurfaceRegistrations& rows) noexcept {
    if (!movie_surfaces_registered(rows)) return false;
    for (unsigned i=1;i<=6;++i)
        if (rows[i].selected!=movie_surface_definitions[i-1]) return false;
    return true;
}
}
