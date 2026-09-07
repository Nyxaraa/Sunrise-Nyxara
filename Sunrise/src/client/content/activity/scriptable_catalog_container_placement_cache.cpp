#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "scriptable_catalog_container_placements_internal.h"

namespace sunrise::client::content::activity::scriptables::internal::container_placements {
namespace {

/** Copies every placement member onto deterministic zeroed storage. */
void copy_placement_row(const catalog::ContainerPlacement& source,
                        catalog::ContainerPlacement& output) noexcept {
    zero_row_storage(output);
    output.listRow = source.listRow;
    output.objectListTag = source.objectListTag;
    output.entryIndex = source.entryIndex;
    output.classListTag = source.classListTag;
    output.classListNameRow = source.classListNameRow;
    output.firstConfig = source.firstConfig;
    output.configCount = source.configCount;
    output.rotation = source.rotation;
    output.position = source.position;
    output.uniformScale = source.uniformScale;
    output.placementIdentifier = source.placementIdentifier;
    output.objectType = source.objectType;
    output.placementIdentifierRead = source.placementIdentifierRead;
    output.complete = source.complete;
}

/** Copies every config member onto deterministic zeroed storage. */
void copy_config_row(const catalog::ContainerPlacementConfig& source,
                     catalog::ContainerPlacementConfig& output) noexcept {
    zero_row_storage(output);
    output.placementRow = source.placementRow;
    output.configTag = source.configTag;
    output.configNameRow = source.configNameRow;
    output.firstComponent = source.firstComponent;
    output.componentCount = source.componentCount;
    output.buildOrdinal = source.buildOrdinal;
    output.secondWord = source.secondWord;
    output.thirdWord = source.thirdWord;
    output.complete = source.complete;
}

/** Captures one exact evidence slice with every byte offset local to that slice. */
[[nodiscard]] bool capture_evidence(const catalog::Snapshot& output,
                                    std::size_t firstInlineName,
                                    std::size_t firstInlineByte,
                                    ContainerPlacementCacheAccess::Evidence& cached) noexcept {
    if (firstInlineName > output.inlineNameCandidates.size()
        || firstInlineByte > output.inlineNameBytes.size()
        || firstInlineByte > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    try {
        cached = {};
        cached.bytes.assign(output.inlineNameBytes.begin()
                                + static_cast<std::ptrdiff_t>(firstInlineByte),
                            output.inlineNameBytes.end());
        cached.rows.reserve(output.inlineNameCandidates.size() - firstInlineName);
        for (std::size_t index = firstInlineName; index < output.inlineNameCandidates.size();
             ++index) {
            catalog::InlineNameCandidate row = output.inlineNameCandidates[index];
            if (row.firstByte < firstInlineByte) {
                return false;
            }
            const std::size_t relative = row.firstByte - firstInlineByte;
            if (relative > cached.bytes.size() || row.byteCount > cached.bytes.size() - relative) {
                return false;
            }
            row.firstByte = static_cast<std::uint32_t>(relative);
            cached.rows.push_back(row);
        }
        return true;
    } catch (...) {
        return false;
    }
}

/** Replays one exact evidence slice or leaves both destination banks unchanged. */
[[nodiscard]] CacheReplay
replay_evidence(BuildContext& context,
                const ContainerPlacementCacheAccess::Evidence& cached) noexcept {
    if (cancelled(context)) {
        return CacheReplay::cancelled;
    }
    catalog::Snapshot& output = *context.output;
    // Snapshot rows are published as u32, so a bank stops at that maximum.
    constexpr std::size_t maximum = (std::numeric_limits<std::uint32_t>::max)();
    const std::size_t firstInlineName = output.inlineNameCandidates.size();
    const std::size_t firstInlineByte = output.inlineNameBytes.size();
    if (firstInlineName > maximum || cached.rows.size() > maximum - firstInlineName
        || firstInlineByte > maximum || cached.bytes.size() > maximum - firstInlineByte) {
        return CacheReplay::unavailable;
    }
    try {
        output.inlineNameCandidates.reserve(firstInlineName + cached.rows.size());
        output.inlineNameBytes.reserve(firstInlineByte + cached.bytes.size());
    } catch (...) {
        return CacheReplay::unavailable;
    }
    output.inlineNameBytes.insert(
        output.inlineNameBytes.end(), cached.bytes.begin(), cached.bytes.end());
    for (catalog::InlineNameCandidate row : cached.rows) {
        if (cancelled(context)) {
            output.inlineNameCandidates.resize(firstInlineName);
            output.inlineNameBytes.resize(firstInlineByte);
            return CacheReplay::cancelled;
        }
        row.firstByte += static_cast<std::uint32_t>(firstInlineByte);
        output.inlineNameCandidates.push_back(row);
    }
    if (cancelled(context)) {
        output.inlineNameCandidates.resize(firstInlineName);
        output.inlineNameBytes.resize(firstInlineByte);
        return CacheReplay::cancelled;
    }
    return CacheReplay::complete;
}

/** @return True when a cached complete graph fits every remaining scenario bank. */
[[nodiscard]] bool graph_fits(const catalog::Snapshot& output,
                              const ContainerPlacementCacheAccess::Value& cached) noexcept {
    return output.containerPlacements.size() <= kPlacementCapacity
           && cached.placements.size() <= kPlacementCapacity - output.containerPlacements.size()
           && output.containerPlacementConfigs.size() <= kConfigCapacity
           && cached.configs.size() <= kConfigCapacity - output.containerPlacementConfigs.size()
           && output.containerPlacementComponents.size() <= kComponentCapacity
           && cached.components.size()
                  <= kComponentCapacity - output.containerPlacementComponents.size();
}

} // namespace

/** Captures one complete list graph with every row index local to that graph. */
bool capture_cached_graph(const catalog::Snapshot& output,
                          std::uint32_t listRow,
                          std::size_t firstPlacement,
                          std::size_t firstConfig,
                          std::size_t firstComponent,
                          std::size_t firstInlineName,
                          std::size_t firstInlineByte,
                          ContainerPlacementCacheAccess::Value& cached) noexcept {
    try {
        cached = {};
        cached.placements.assign(output.containerPlacements.begin()
                                     + static_cast<std::ptrdiff_t>(firstPlacement),
                                 output.containerPlacements.end());
        cached.configs.assign(output.containerPlacementConfigs.begin()
                                  + static_cast<std::ptrdiff_t>(firstConfig),
                              output.containerPlacementConfigs.end());
        cached.components.assign(output.containerPlacementComponents.begin()
                                     + static_cast<std::ptrdiff_t>(firstComponent),
                                 output.containerPlacementComponents.end());
        for (catalog::ContainerPlacement& placement : cached.placements) {
            if (placement.listRow != listRow || placement.firstConfig < firstConfig
                || placement.firstConfig - firstConfig > cached.configs.size()) {
                return false;
            }
            placement.listRow = 0;
            placement.firstConfig -= static_cast<std::uint32_t>(firstConfig);
        }
        for (catalog::ContainerPlacementConfig& config : cached.configs) {
            if (config.placementRow < firstPlacement
                || config.placementRow - firstPlacement >= cached.placements.size()
                || config.firstComponent < firstComponent
                || config.firstComponent - firstComponent > cached.components.size()) {
                return false;
            }
            config.placementRow -= static_cast<std::uint32_t>(firstPlacement);
            config.firstComponent -= static_cast<std::uint32_t>(firstComponent);
        }
        for (catalog::ContainerPlacementComponent& component : cached.components) {
            if (component.configRow < firstConfig
                || component.configRow - firstConfig >= cached.configs.size()) {
                return false;
            }
            component.configRow -= static_cast<std::uint32_t>(firstConfig);
        }
        return capture_evidence(output, firstInlineName, firstInlineByte, cached.evidence);
    } catch (...) {
        return false;
    }
}

/** Replays one complete graph or leaves every destination bank unchanged. */
CacheReplay replay_cached_graph(BuildContext& context,
                                std::uint32_t listRow,
                                const ContainerPlacementCacheAccess::Value& cached) noexcept {
    if (cancelled(context)) {
        return CacheReplay::cancelled;
    }
    catalog::Snapshot& output = *context.output;
    if (!graph_fits(output, cached)) {
        return CacheReplay::unavailable;
    }
    // Snapshot rows are published as u32, so a bank stops at that maximum.
    constexpr std::size_t maximum = (std::numeric_limits<std::uint32_t>::max)();
    const std::size_t firstPlacement = output.containerPlacements.size();
    const std::size_t firstConfig = output.containerPlacementConfigs.size();
    const std::size_t firstComponent = output.containerPlacementComponents.size();
    const std::size_t firstInlineName = output.inlineNameCandidates.size();
    const std::size_t firstInlineByte = output.inlineNameBytes.size();
    if (firstInlineName > maximum || cached.evidence.rows.size() > maximum - firstInlineName
        || firstInlineByte > maximum || cached.evidence.bytes.size() > maximum - firstInlineByte) {
        return CacheReplay::unavailable;
    }
    try {
        output.containerPlacements.reserve(firstPlacement + cached.placements.size());
        output.containerPlacementConfigs.reserve(firstConfig + cached.configs.size());
        output.containerPlacementComponents.reserve(firstComponent + cached.components.size());
        output.inlineNameCandidates.reserve(firstInlineName + cached.evidence.rows.size());
        output.inlineNameBytes.reserve(firstInlineByte + cached.evidence.bytes.size());
    } catch (...) {
        return CacheReplay::unavailable;
    }
    const auto rollback = [&output,
                           firstPlacement,
                           firstConfig,
                           firstComponent,
                           firstInlineName,
                           firstInlineByte]() noexcept {
        output.containerPlacements.resize(firstPlacement);
        output.containerPlacementConfigs.resize(firstConfig);
        output.containerPlacementComponents.resize(firstComponent);
        output.inlineNameCandidates.resize(firstInlineName);
        output.inlineNameBytes.resize(firstInlineByte);
    };
    output.inlineNameBytes.insert(
        output.inlineNameBytes.end(), cached.evidence.bytes.begin(), cached.evidence.bytes.end());
    for (catalog::InlineNameCandidate row : cached.evidence.rows) {
        if (cancelled(context)) {
            rollback();
            return CacheReplay::cancelled;
        }
        row.firstByte += static_cast<std::uint32_t>(firstInlineByte);
        output.inlineNameCandidates.push_back(row);
    }
    for (const catalog::ContainerPlacement& source : cached.placements) {
        if (cancelled(context)) {
            rollback();
            return CacheReplay::cancelled;
        }
        output.containerPlacements.emplace_back();
        catalog::ContainerPlacement& row = output.containerPlacements.back();
        copy_placement_row(source, row);
        row.listRow = listRow;
        row.firstConfig += static_cast<std::uint32_t>(firstConfig);
    }
    for (const catalog::ContainerPlacementConfig& source : cached.configs) {
        if (cancelled(context)) {
            rollback();
            return CacheReplay::cancelled;
        }
        output.containerPlacementConfigs.emplace_back();
        catalog::ContainerPlacementConfig& row = output.containerPlacementConfigs.back();
        copy_config_row(source, row);
        row.placementRow += static_cast<std::uint32_t>(firstPlacement);
        row.firstComponent += static_cast<std::uint32_t>(firstComponent);
    }
    for (catalog::ContainerPlacementComponent row : cached.components) {
        if (cancelled(context)) {
            rollback();
            return CacheReplay::cancelled;
        }
        row.configRow += static_cast<std::uint32_t>(firstConfig);
        output.containerPlacementComponents.push_back(row);
    }
    if (cancelled(context)) {
        rollback();
        return CacheReplay::cancelled;
    }
    return CacheReplay::complete;
}

} // namespace sunrise::client::content::activity::scriptables::internal::container_placements
