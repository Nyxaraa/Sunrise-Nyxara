/**
 * The animated Sunrise logo. One bundled sprite sheet is uploaded as a single texture, and each
 * frame is picked out of it with texture coordinates.
 */

#include "ui_logo_component.h"

#include <cmath>
#include <imgui.h>

#include "../../textures/ui_texture_slots.h"

namespace sunrise::core::ui::components::logo {
namespace {

/** Sheet geometry of the bundled logo. It is a square 11 by 11 grid of square frames. */
constexpr float kSheetExtent = 1408.0F;
constexpr float kFrameExtent = 128.0F;
constexpr int kFrameColumns = 11;
/** The animation uses 112 of the 121 cells. The 9 cells after it are empty. */
constexpr int kFrameCount = 112;
/** Every frame is shown for the same 33 ms, so one loop runs 3.696 seconds. */
constexpr double kFrameSeconds = 0.033;
/** The art is grayscale, so this gold pair colors it, deep at the top and light at the bottom. */
constexpr ImU32 kGradientTop = IM_COL32(0xCB, 0x9B, 0x51, 0xFF);
constexpr ImU32 kGradientBottom = IM_COL32(0xF6, 0xE2, 0x7A, 0xFF);
/** One quad is two triangles: six indices over four corner vertices. */
constexpr int kQuadIndexCount = 6;
constexpr int kQuadVertexCount = 4;
/** Offsets from the quad's first vertex, which is written top-left first and then clockwise. */
constexpr ImDrawIdx kTopRightOffset = 1;
constexpr ImDrawIdx kBottomRightOffset = 2;
constexpr ImDrawIdx kBottomLeftOffset = 3;

/** @return The sheet cell to show now. */
[[nodiscard]] int current_frame() noexcept {
    const double elapsed = ImGui::GetTime() / kFrameSeconds;
    return static_cast<int>(std::fmod(elapsed, static_cast<double>(kFrameCount)));
}

/**
 * Writes one textured quad whose color runs down its vertical axis. Dear ImGui has no
 * multi-color image call, so the four corner vertices carry the two colors themselves.
 * @param sheet Texture the quad samples.
 * @param minimum Top-left screen corner.
 * @param maximum Bottom-right screen corner.
 * @param uvMinimum Texture coordinate of the top-left corner.
 * @param uvMaximum Texture coordinate of the bottom-right corner.
 */
void draw_gradient_quad(ImTextureID sheet,
                        const ImVec2& minimum,
                        const ImVec2& maximum,
                        const ImVec2& uvMinimum,
                        const ImVec2& uvMaximum) noexcept {
    // Both colors take the style alpha, so the logo fades with the surface that holds it.
    const ImU32 top = ImGui::GetColorU32(kGradientTop);
    const ImU32 bottom = ImGui::GetColorU32(kGradientBottom);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushTexture(ImTextureRef(sheet));
    drawList->PrimReserve(kQuadIndexCount, kQuadVertexCount);
    const auto first = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
    drawList->PrimWriteIdx(first);
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + kTopRightOffset));
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + kBottomRightOffset));
    drawList->PrimWriteIdx(first);
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + kBottomRightOffset));
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + kBottomLeftOffset));
    drawList->PrimWriteVtx(minimum, uvMinimum, top);
    drawList->PrimWriteVtx({maximum.x, minimum.y}, {uvMaximum.x, uvMinimum.y}, top);
    drawList->PrimWriteVtx(maximum, uvMaximum, bottom);
    drawList->PrimWriteVtx({minimum.x, maximum.y}, {uvMinimum.x, uvMaximum.y}, bottom);
    drawList->PopTexture();
}

} // namespace

/** Draws the current frame of the animated Sunrise logo at the cursor. */
bool draw(float extent) noexcept {
    const ImTextureID sheet = textures::get(textures::Slot::logoSheet);
    if (sheet == ImTextureID_Invalid) {
        return false;
    }
    const int frame = current_frame();
    const float step = kFrameExtent / kSheetExtent;
    const float column = static_cast<float>(frame % kFrameColumns) * step;
    const float row = static_cast<float>(frame / kFrameColumns) * step;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    draw_gradient_quad(sheet,
                       origin,
                       {origin.x + extent, origin.y + extent},
                       {column, row},
                       {column + step, row + step});
    // The quad goes straight into the draw list, so the layout is told what space it took.
    ImGui::Dummy({extent, extent});
    return true;
}

} // namespace sunrise::core::ui::components::logo
