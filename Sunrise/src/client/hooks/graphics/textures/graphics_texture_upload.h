#pragma once

#include <d3d11.h>

namespace sunrise::client::hooks::graphics::textures {

/** One uploaded image: the texture we own and the view the interface draws with. */
struct Uploaded {
    ID3D11Texture2D* texture{};
    ID3D11ShaderResourceView* view{};
};

/**
 * Decodes the bundled logo sheet and publishes its view to the Core interface.
 * @param device Device that creates and owns the texture.
 * @param output Receives the created objects. Left alone when any step fails.
 * @return True when the sheet is uploaded and published.
 */
[[nodiscard]] bool upload_logo_sheet(ID3D11Device* device, Uploaded& output) noexcept;

/** @param uploaded Objects released and cleared, after the published slot is emptied. */
void release_logo_sheet(Uploaded& uploaded) noexcept;

} // namespace sunrise::client::hooks::graphics::textures
