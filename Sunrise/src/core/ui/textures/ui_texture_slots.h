#pragma once

#include <cstdint>
#include <imgui.h>

namespace sunrise::core::ui::textures {

/** One slot per bundled image the interface can draw. */
enum class Slot : std::uint8_t {
    /** Grayscale sprite sheet of the animated Sunrise logo. */
    logoSheet,
    count,
};

/**
 * Publishes the renderer identifier of one uploaded image.
 * The presentation thread owns every slot: it uploads, publishes and frees the textures.
 * @param slot Image the identifier belongs to.
 * @param texture Renderer identifier, or ImTextureID_Invalid to empty the slot.
 */
void publish(Slot slot, ImTextureID texture) noexcept;

/** @param slot Image to read. @return Its identifier, or ImTextureID_Invalid while unloaded. */
[[nodiscard]] ImTextureID get(Slot slot) noexcept;

/** Empties every slot. The renderer owns the textures and frees them itself. */
void clear() noexcept;

} // namespace sunrise::core::ui::textures
