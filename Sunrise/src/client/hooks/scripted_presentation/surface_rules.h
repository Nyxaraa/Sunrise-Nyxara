#pragma once
#include <array>
#include <cstdint>
namespace sunrise::client::hooks::scripted_presentation {
// Type-19 definitions expand from eight package bytes to a 16-byte native
// allocation. The raw buffer loader later fills definition+8 (native 1204581).
constexpr bool movie_definition_resident(std::uint32_t size,
                                         std::uint32_t typeInfo,
                                         std::uintptr_t pointer) noexcept {
    return size == 0xC0000010U && (typeInfo & 0x3FFFFU) == 0x44FBU && pointer != 0;
}
constexpr bool
movie_definition_matches(unsigned index, std::uint8_t slot, std::uint8_t format) noexcept {
    return index < 6 && slot == index + 1 && format == (index < 2 ? 0x7F : 0x23);
}
struct SurfaceRegistration {
    std::uint32_t count{}, entries[3]{}, selected{0xFFFFFFFFU};
};
static_assert(sizeof(SurfaceRegistration) == 20);
using SurfaceRegistrations = std::array<SurfaceRegistration, 8>;
constexpr bool movie_surfaces_absent(const SurfaceRegistrations& rows,
                                     const std::array<std::uint32_t, 6>& definitions) noexcept {
    for (const auto& row : rows) {
        if (row.count > 3) return false;
        for (auto definition : definitions) {
            if (row.selected == definition) return false;
            for (unsigned i = 0; i < row.count; ++i)
                if (row.entries[i] == definition) return false;
        }
    }
    return true;
}
constexpr std::uint32_t selected_surface(const SurfaceRegistration& r) noexcept {
    return r.count > 0 && r.count <= 3 ? r.entries[r.count - 1] : 0xFFFFFFFFU;
}
constexpr bool movie_surfaces_registered(const SurfaceRegistrations& rows,
                                         const std::array<std::uint32_t, 6>& definitions) noexcept {
    for (unsigned i = 0; i < rows.size(); ++i) {
        if (rows[i].count > 3) return false;
        const auto top = selected_surface(rows[i]);
        // The native publisher updates all eight slots. Do not change unrelated
        // pending slot 0/7 selections while preparing a scripted movie.
        if (i == 0 || i == 7) {
            if (rows[i].selected != top) return false;
        } else if (top != definitions[i - 1])
            return false;
    }
    return true;
}
constexpr bool movie_surfaces_selected(const SurfaceRegistrations& rows,
                                       const std::array<std::uint32_t, 6>& definitions) noexcept {
    if (!movie_surfaces_registered(rows, definitions)) return false;
    for (unsigned i = 1; i <= 6; ++i)
        if (rows[i].selected != definitions[i - 1]) return false;
    return true;
}
} // namespace sunrise::client::hooks::scripted_presentation
