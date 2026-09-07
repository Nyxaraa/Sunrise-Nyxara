#include "ui_texture_slots.h"

#include <array>
#include <cstddef>

namespace sunrise::core::ui::textures {
namespace {

/** Published identifiers. Only the presentation thread reads or writes them. */
std::array<ImTextureID, static_cast<std::size_t>(Slot::count)> g_textures{};

/** @param slot Slot to check. @return True when it names one of the images. */
[[nodiscard]] bool in_range(Slot slot) noexcept {
    return static_cast<std::size_t>(slot) < g_textures.size();
}

} // namespace

/** Publishes the renderer identifier of one uploaded image. */
void publish(Slot slot, ImTextureID texture) noexcept {
    if (in_range(slot)) {
        g_textures[static_cast<std::size_t>(slot)] = texture;
    }
}

/** @return The slot's identifier, or ImTextureID_Invalid while it is unloaded. */
ImTextureID get(Slot slot) noexcept {
    return in_range(slot) ? g_textures[static_cast<std::size_t>(slot)] : ImTextureID_Invalid;
}

/** Empties every slot. The renderer owns the textures and frees them itself. */
void clear() noexcept {
    g_textures = {};
}

} // namespace sunrise::core::ui::textures
