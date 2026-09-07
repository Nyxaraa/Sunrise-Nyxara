#include "authored_spatial_overlay.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

#include "../../../state/build_data/scriptables/scriptable_catalog.h"
#include "../../hooks/graphics/renderer/world_lines.h"
#include "authored_placement_marker.h"
#include "package_trigger_volume_geometry.h"

namespace sunrise::client::ui::activity::authored_spatial_overlay {
namespace {

namespace lines = hooks::graphics::renderer::world_lines;
namespace marker = authored_placement_marker;
namespace trigger_geometry = package_trigger_volume_geometry;

// Fixed edge budget for one frame of trigger prisms.
constexpr std::size_t kTriggerEdgeCapacity = 4'096;
std::atomic<std::size_t> g_triggerVolumes{};
std::atomic<std::size_t> g_triggerEdges{};
std::atomic<std::size_t> g_invalidTriggerVolumes{};
std::atomic<bool> g_triggerEdgeCapacityExceeded{};

/** Converts one bounded UI colour to the renderer's byte layout. */
[[nodiscard]] lines::Color line_color(marker::MarkerColor channels) noexcept {
    // Channels are held 0..1 and drawn as bytes.
    constexpr float scale = 255.0F;
    for (float& channel : channels) {
        channel = std::isfinite(channel) ? std::clamp(channel, 0.0F, 1.0F) : 1.0F;
    }
    return {static_cast<std::uint8_t>(channels[0] * scale + 0.5F),
            static_cast<std::uint8_t>(channels[1] * scale + 0.5F),
            static_cast<std::uint8_t>(channels[2] * scale + 0.5F),
            static_cast<std::uint8_t>(channels[3] * scale + 0.5F)};
}

/** @return An AABB centred on one point with a user-authored diagnostic extent. */
[[nodiscard]] lines::Box
diagnostic_box(const marker::Anchor& anchor, float extent, lines::Color color) noexcept {
    lines::Box box{};
    box.color = color;
    for (std::size_t lane = 0; lane < anchor.position.size(); ++lane) {
        box.minimum[lane] = anchor.position[lane] - extent;
        box.maximum[lane] = anchor.position[lane] + extent;
    }
    return box;
}

} // namespace

/** Draws the current package-anchor scope as depth-independent D3D11 world lines. */
bool draw(ID3D11Device* device,
          ID3D11DeviceContext* context,
          ID3D11RenderTargetView* target,
          const marker::RenderSet& source,
          const hooks::teleport::CameraPose& camera) noexcept {
    std::array<lines::Point, marker::kRenderCapacity> points{};
    std::array<lines::Axes, marker::kRenderCapacity> axes{};
    std::array<lines::Box, marker::kRenderCapacity> boxes{};
    std::array<lines::Sphere, marker::kRenderCapacity> spheres{};
    std::array<lines::Edge, kTriggerEdgeCapacity> edges{};
    std::size_t pointCount = 0;
    std::size_t axesCount = 0;
    std::size_t boxCount = 0;
    std::size_t sphereCount = 0;
    std::size_t edgeCount = 0;
    std::size_t triggerVolumeCount = 0;
    std::size_t invalidTriggerVolumeCount = 0;
    bool triggerEdgeCapacityExceeded = false;
    lines::Batch batch{};
    batch.lineWidthPixels = source.options.worldLineWidth;
    batch.invertX = source.options.invertX;
    batch.invertY = source.options.invertY;
    const state::build_data::scriptables::SnapshotView catalog =
        state::build_data::scriptables::snapshot();
    for (std::size_t index = 0; index < source.count; ++index) {
        const marker::Anchor& anchor = source.anchors[index];
        if (anchor.sourceKind == marker::AnchorSource::packageAabb) {
            continue;
        }
        const lines::Color color =
            line_color(marker::source_color(source.options, anchor.sourceKind));
        if (anchor.sourceKind == marker::AnchorSource::packageTriggerVolume) {
            if (catalog == nullptr || catalog->revision != source.context.dynamicCatalogRevision
                || catalog->scenarioTag != source.context.scenarioTag) {
                ++invalidTriggerVolumeCount;
                continue;
            }
            const trigger_geometry::Result result =
                trigger_geometry::build(*catalog,
                                        anchor.sourceRow,
                                        color,
                                        std::span<lines::Edge>(edges).subspan(edgeCount));
            if (!result.valid) {
                invalidTriggerVolumeCount += result.capacityExceeded ? 0U : 1U;
                triggerEdgeCapacityExceeded =
                    triggerEdgeCapacityExceeded || result.capacityExceeded;
                continue;
            }
            edgeCount += result.count;
            ++triggerVolumeCount;
            continue;
        }
        switch (source.options.worldGlyph) {
        case marker::WorldGlyph::point:
            points[pointCount++] = {anchor.position, source.options.worldGlyphSize, color};
            break;
        case marker::WorldGlyph::axes:
            axes[axesCount++] = {anchor.position, source.options.worldGlyphSize, color};
            break;
        case marker::WorldGlyph::diagnosticBox:
            boxes[boxCount++] = diagnostic_box(anchor, source.options.worldGlyphSize, color);
            break;
        case marker::WorldGlyph::diagnosticSphere:
            spheres[sphereCount++] = {anchor.position, source.options.worldGlyphSize, 16, color};
            break;
        }
    }
    batch.points = std::span<const lines::Point>(points.data(), pointCount);
    batch.axes = std::span<const lines::Axes>(axes.data(), axesCount);
    batch.boxes = std::span<const lines::Box>(boxes.data(), boxCount);
    batch.spheres = std::span<const lines::Sphere>(spheres.data(), sphereCount);
    batch.edges = std::span<const lines::Edge>(edges.data(), edgeCount);
    const lines::Result result = lines::draw(device, context, target, camera, batch);
    g_triggerVolumes.store(triggerVolumeCount, std::memory_order_release);
    g_triggerEdges.store(edgeCount, std::memory_order_release);
    g_invalidTriggerVolumes.store(invalidTriggerVolumeCount, std::memory_order_release);
    g_triggerEdgeCapacityExceeded.store(triggerEdgeCapacityExceeded || result.truncated,
                                        std::memory_order_release);
    return result.rendered;
}

/** Copies the most recent package-trigger overlay diagnostics. */
Diagnostics diagnostics() noexcept {
    return {.volumes = g_triggerVolumes.load(std::memory_order_acquire),
            .edges = g_triggerEdges.load(std::memory_order_acquire),
            .invalidVolumes = g_invalidTriggerVolumes.load(std::memory_order_acquire),
            .edgeCapacityExceeded = g_triggerEdgeCapacityExceeded.load(std::memory_order_acquire)};
}

} // namespace sunrise::client::ui::activity::authored_spatial_overlay
