#pragma once

#include <cstddef>
#include <d3d11.h>

#include "../../hooks/teleport/runtime.h"
#include "authored_placement_marker.h"

namespace sunrise::client::ui::activity::authored_spatial_overlay {

/** Bounded package-trigger geometry observed by the most recent overlay draw. */
struct Diagnostics final {
    std::size_t volumes{};
    std::size_t edges{};
    std::size_t invalidVolumes{};
    bool edgeCapacityExceeded{};
};

/** Draws one resolved package-anchor scope as depth-independent D3D11 world lines. */
[[nodiscard]] bool draw(ID3D11Device* device,
                        ID3D11DeviceContext* context,
                        ID3D11RenderTargetView* target,
                        const authored_placement_marker::RenderSet& source,
                        const hooks::teleport::CameraPose& camera) noexcept;

/** Copies the most recent package-trigger overlay diagnostics. */
[[nodiscard]] Diagnostics diagnostics() noexcept;

} // namespace sunrise::client::ui::activity::authored_spatial_overlay
