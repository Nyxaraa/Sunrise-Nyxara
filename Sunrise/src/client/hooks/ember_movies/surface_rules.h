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
constexpr std::array<std::uint32_t,6> movie_surface_buffers{
    0x80BCA020U,0x80BCA023U,0x80BCA027U,0x80BCA02AU,0x80BCA02DU,0x80BCA030U};
// Type-19 definitions expand from eight package bytes to a 16-byte native
// allocation. The raw buffer loader later fills definition+8 (native 1204581).
constexpr bool movie_definition_resident(std::uint32_t size, std::uint32_t typeInfo,
                                         std::uintptr_t pointer) noexcept {
    return size==0xC0000010U
        && (typeInfo&0x3FFFFU)==0x44FBU && pointer!=0;
}
constexpr bool movie_definition_matches(unsigned index, std::uint8_t slot,
                                        std::uint8_t format) noexcept {
    return index<6 && slot==index+1 && format==(index<2 ? 0x7F : 0x23);
}
struct SurfaceRegistration {
    std::uint32_t count{}, entries[3]{}, selected{0xFFFFFFFFU};
};
static_assert(sizeof(SurfaceRegistration)==20);
using SurfaceRegistrations=std::array<SurfaceRegistration,8>;
constexpr bool movie_surfaces_absent(const SurfaceRegistrations& rows) noexcept {
    for (const auto& row : rows) {
        if (row.count>3) return false;
        for (auto definition : movie_surface_definitions) {
            if (row.selected==definition) return false;
            for (unsigned i=0;i<row.count;++i)
                if (row.entries[i]==definition) return false;
        }
    }
    return true;
}
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
