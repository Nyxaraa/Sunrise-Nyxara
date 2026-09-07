#include "package_trigger_volume_geometry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

#include "../../../state/build_data/scriptables/scriptable_catalog.h"

namespace sunrise::client::ui::activity::package_trigger_volume_geometry {
namespace {

namespace catalog = state::build_data::scriptables;
namespace lines = hooks::graphics::renderer::world_lines;

// Triangle indices are bytes, so every undirected edge key fits one table.
constexpr std::size_t kIndexCardinality =
    static_cast<std::size_t>((std::numeric_limits<std::uint8_t>::max)()) + 1U;
// One slot per ordered index pair keeps the edge table a flat array.
constexpr std::size_t kEdgeCardinality = kIndexCardinality * kIndexCardinality;

/** @return The canonical key for one undirected byte-indexed edge. */
[[nodiscard]] constexpr std::size_t edge_key(std::uint8_t first, std::uint8_t second) noexcept {
    const std::uint8_t low = (std::min)(first, second);
    const std::uint8_t high = (std::max)(first, second);
    return static_cast<std::size_t>(low) * kIndexCardinality + high;
}

/** Adds one world edge without allowing a partial prism. */
void edge(const catalog::TriggerVolumeVertex& first,
          const catalog::TriggerVolumeVertex& second,
          float zOffset,
          lines::Color color,
          lines::Edge& output) noexcept {
    output.first = {first.value[0], first.value[1], first.value[2] + zOffset};
    output.second = {second.value[0], second.value[1], second.value[2] + zOffset};
    output.color = color;
}

} // namespace

/** @return True only for the bit-exact identity SpawnEntry transform proved by the corpus. */
bool supported_transform(const catalog::TriggerVolumeInstance& instance) noexcept {
    // Identity transform: zero lanes and a unit w.
    constexpr std::array<std::uint32_t, 4> identity{0, 0, 0, 0x3F800000U};
    for (std::size_t lane = 0; lane < identity.size(); ++lane) {
        if (std::bit_cast<std::uint32_t>(instance.rotation[lane]) != identity[lane]
            || std::bit_cast<std::uint32_t>(instance.position[lane]) != identity[lane]) {
            return false;
        }
    }
    return true;
}

/** Materializes the exact boundary of one package-authored +Z-extruded triangle mesh. */
Result build(const catalog::Snapshot& source,
             std::uint32_t instanceRow,
             lines::Color color,
             std::span<lines::Edge> output) noexcept {
    Result result{};
    if (instanceRow >= source.triggerVolumeInstances.size()) {
        return result;
    }
    const catalog::TriggerVolumeInstance& instance = source.triggerVolumeInstances[instanceRow];
    catalog::TriggerVolumeView view{};
    view.tables = source.triggerVolumeTables;
    view.owners = source.triggerVolumeOwners;
    view.instances = source.triggerVolumeInstances;
    view.vertices = source.triggerVolumeVertices;
    view.triangles = source.triggerVolumeTriangles;
    view.diagnostics = source.triggerVolumeDiagnostics;
    const auto vertices = catalog::trigger_volume_vertices(view, instance);
    const auto triangles = catalog::trigger_volume_triangles(view, instance);
    if (!instance.complete || instance.active == 0 || !supported_transform(instance)
        || vertices.empty() || triangles.empty() || vertices.size() > kIndexCardinality
        || !std::isfinite(instance.extrusion) || instance.extrusion < 0.0F) {
        return result;
    }

    std::array<std::uint8_t, kEdgeCardinality> edgeUses{};
    for (const catalog::TriggerVolumeTriangle& triangle : triangles) {
        for (std::uint8_t index : triangle.indices) {
            if (index >= vertices.size()) {
                return result;
            }
        }
        // The three undirected edges of one triangle.
        constexpr std::array<std::array<std::size_t, 2>, 3> pairs{{{0, 1}, {1, 2}, {2, 0}}};
        for (const auto& pair : pairs) {
            std::uint8_t& uses =
                edgeUses[edge_key(triangle.indices[pair[0]], triangle.indices[pair[1]])];
            if (uses != (std::numeric_limits<std::uint8_t>::max)()) {
                ++uses;
            }
        }
    }

    std::array<bool, kIndexCardinality> boundaryVertices{};
    std::size_t boundaryEdges = 0;
    for (std::size_t low = 0; low < vertices.size(); ++low) {
        for (std::size_t high = low + 1; high < vertices.size(); ++high) {
            if (edgeUses[low * kIndexCardinality + high] == 1) {
                ++boundaryEdges;
                boundaryVertices[low] = true;
                boundaryVertices[high] = true;
            }
        }
    }
    const auto vertexCount = static_cast<std::ptrdiff_t>(vertices.size());
    const std::size_t boundaryVertexCount = static_cast<std::size_t>(
        std::count(boundaryVertices.begin(), boundaryVertices.begin() + vertexCount, true));
    const std::size_t required = boundaryEdges * 2U + boundaryVertexCount;
    if (required == 0) {
        return result;
    }
    if (required > output.size()) {
        result.capacityExceeded = true;
        return result;
    }

    std::size_t cursor = 0;
    for (std::size_t low = 0; low < vertices.size(); ++low) {
        for (std::size_t high = low + 1; high < vertices.size(); ++high) {
            if (edgeUses[low * kIndexCardinality + high] != 1) {
                continue;
            }
            edge(vertices[low], vertices[high], 0.0F, color, output[cursor++]);
            edge(vertices[low], vertices[high], instance.extrusion, color, output[cursor++]);
        }
    }
    for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex) {
        if (!boundaryVertices[vertex]) {
            continue;
        }
        output[cursor].first = {
            vertices[vertex].value[0], vertices[vertex].value[1], vertices[vertex].value[2]};
        output[cursor].second = {vertices[vertex].value[0],
                                 vertices[vertex].value[1],
                                 vertices[vertex].value[2] + instance.extrusion};
        output[cursor].color = color;
        ++cursor;
    }
    result.count = cursor;
    result.valid = cursor == required;
    return result;
}

} // namespace sunrise::client::ui::activity::package_trigger_volume_geometry
