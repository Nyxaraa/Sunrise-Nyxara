#include "activity_sdk_squad_graph_shard.h"

#include <limits>
#include <utility>
#include <vector>

namespace sunrise::client::content::activity::sdk_generation::squad_inventory {
namespace {

namespace catalog = state::build_data::scriptables;

/** @return True when a source global row maps to a scenario-local row. */
[[nodiscard]] bool mapped_row(const std::vector<std::uint32_t>& rows,
                              std::uint32_t globalRow,
                              std::uint32_t& output) noexcept {
    output = catalog::kNoRow;
    if (globalRow >= rows.size() || rows[globalRow] == catalog::kNoRow) {
        return false;
    }
    output = rows[globalRow];
    return true;
}

/** Copies config and placement occurrences while constructing exact local-row maps. */
[[nodiscard]] bool append_occurrences(std::uint32_t scenarioIndex,
                                      const GraphSnapshot& graph,
                                      catalog::Snapshot& output,
                                      std::vector<std::uint32_t>& configRows,
                                      std::vector<std::uint32_t>& placementRows) {
    configRows.assign(graph.configContexts.size(), catalog::kNoRow);
    placementRows.assign(graph.placementContexts.size(), catalog::kNoRow);
    for (std::uint32_t row = 0; row < graph.configContexts.size(); ++row) {
        const GraphConfigContext& source = graph.configContexts[row];
        if (source.globalRow != row) {
            return false;
        }
        if (source.scenarioIndex != scenarioIndex) {
            continue;
        }
        configRows[row] = static_cast<std::uint32_t>(output.authoredSquadConfigContexts.size());
        output.authoredSquadConfigContexts.push_back({source.globalRow,
                                                      source.scenarioIndex,
                                                      source.configTag,
                                                      source.occurrenceIndex,
                                                      source.objectIndex,
                                                      source.pathOrdinal,
                                                      source.declaredBubbleIndex,
                                                      source.spawnerRow,
                                                      source.ruleRow,
                                                      source.complete});
    }
    for (std::uint32_t row = 0; row < graph.placementContexts.size(); ++row) {
        const GraphPlacementContext& source = graph.placementContexts[row];
        if (source.globalRow != row) {
            return false;
        }
        if (source.scenarioIndex != scenarioIndex) {
            continue;
        }
        placementRows[row] =
            static_cast<std::uint32_t>(output.authoredSquadPlacementContexts.size());
        output.authoredSquadPlacementContexts.push_back({source.globalRow,
                                                         source.scenarioIndex,
                                                         source.occurrenceIndex,
                                                         source.objectListTag,
                                                         source.placementOrdinal,
                                                         source.placedEntryIdentity,
                                                         source.positionBits,
                                                         source.objectIndex,
                                                         source.pathOrdinal,
                                                         source.declaredBubbleIndex,
                                                         source.actorDefinitionTag,
                                                         source.sourceOffset,
                                                         source.quaternionBits,
                                                         source.uniformScaleBits,
                                                         source.nameHash,
                                                         source.placementFlagsRaw,
                                                         source.auxiliaryRelative,
                                                         source.complete});
    }
    return true;
}

/** Copies point cardinalities and remaps every owned match to this shard's local rows. */
[[nodiscard]] bool append_point_matches(std::uint32_t scenarioIndex,
                                        const GraphSnapshot& graph,
                                        const std::vector<std::uint32_t>& configRows,
                                        const std::vector<std::uint32_t>& placementRows,
                                        catalog::Snapshot& output) {
    for (std::uint32_t row = 0; row < graph.pointContexts.size(); ++row) {
        const GraphPointContext& source = graph.pointContexts[row];
        if (source.globalRow != row) {
            return false;
        }
        if (source.scenarioIndex != scenarioIndex) {
            continue;
        }
        std::uint32_t configRow = catalog::kNoRow;
        if (!mapped_row(configRows, source.configContextRow, configRow)
            || source.matches.first > graph.pointPlacementMatches.size()
            || source.matches.count > graph.pointPlacementMatches.size() - source.matches.first) {
            return false;
        }
        const std::uint32_t localPointContextRow =
            static_cast<std::uint32_t>(output.authoredSquadPointContexts.size());
        catalog::AuthoredSquadPointContext context{};
        context.globalRow = source.globalRow;
        context.scenarioIndex = source.scenarioIndex;
        context.pointRow = source.pointRow;
        context.globalConfigContextRow = source.configContextRow;
        context.configContextRow = configRow;
        context.globalFirstMatch = source.matches.first;
        context.firstMatch =
            static_cast<std::uint32_t>(output.authoredSquadPointPlacementMatches.size());
        context.status = static_cast<catalog::AuthoredSquadPointContextStatus>(source.status);
        for (std::uint32_t matchRow = source.matches.first;
             matchRow < source.matches.first + source.matches.count;
             ++matchRow) {
            const GraphPointPlacementMatch& match = graph.pointPlacementMatches[matchRow];
            std::uint32_t matchConfigRow = catalog::kNoRow;
            std::uint32_t placementRow = catalog::kNoRow;
            if (match.globalRow != matchRow || match.scenarioIndex != scenarioIndex
                || match.pointContextRow != row || match.pointRow != source.pointRow
                || match.configContextRow != source.configContextRow
                || !mapped_row(configRows, match.configContextRow, matchConfigRow)
                || !mapped_row(placementRows, match.placementContextRow, placementRow)) {
                return false;
            }
            output.authoredSquadPointPlacementMatches.push_back({match.globalRow,
                                                                 match.scenarioIndex,
                                                                 match.pointContextRow,
                                                                 localPointContextRow,
                                                                 match.pointRow,
                                                                 match.configContextRow,
                                                                 matchConfigRow,
                                                                 match.placementContextRow,
                                                                 placementRow,
                                                                 match.placedEntryIdentity,
                                                                 match.sameOccurrence});
        }
        context.matchCount = static_cast<std::uint32_t>(
            output.authoredSquadPointPlacementMatches.size() - context.firstMatch);
        if (context.matchCount != source.matches.count) {
            return false;
        }
        output.authoredSquadPointContexts.push_back(context);
    }
    return true;
}

/** Copies scenario membership for estate-global authored spawner-rule edges. */
[[nodiscard]] bool append_edge_contexts(std::uint32_t scenarioIndex,
                                        const GraphSnapshot& graph,
                                        catalog::Snapshot& output) {
    for (std::uint32_t row = 0; row < graph.edgeContexts.size(); ++row) {
        const GraphEdgeContext& source = graph.edgeContexts[row];
        if (source.globalRow != row || source.edgeRow >= graph.edges.size()) {
            return false;
        }
        if (source.scenarioIndex == scenarioIndex) {
            output.authoredSquadEdgeContexts.push_back(
                {source.globalRow, source.scenarioIndex, source.edgeRow});
        }
    }
    return true;
}

} // namespace

/** Appends one closed scenario-owned graph projection without publishing global definitions. */
bool append_scenario_graph_contexts(std::uint32_t scenarioIndex,
                                    const GraphSnapshot& graph,
                                    catalog::Snapshot& output) noexcept {
    if (!graph.ready || scenarioIndex == catalog::kNoRow
        || !output.authoredSquadConfigContexts.empty()
        || !output.authoredSquadPlacementContexts.empty()
        || !output.authoredSquadPointContexts.empty()
        || !output.authoredSquadPointPlacementMatches.empty()
        || !output.authoredSquadEdgeContexts.empty() || output.authoredSquadGraphContextsComplete) {
        return false;
    }
    try {
        catalog::Snapshot pending = output;
        std::vector<std::uint32_t> configRows{};
        std::vector<std::uint32_t> placementRows{};
        if (!append_occurrences(scenarioIndex, graph, pending, configRows, placementRows)
            || !append_point_matches(scenarioIndex, graph, configRows, placementRows, pending)
            || !append_edge_contexts(scenarioIndex, graph, pending)) {
            return false;
        }
        pending.authoredSquadGraphContextsComplete = true;
        output = std::move(pending);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace sunrise::client::content::activity::sdk_generation::squad_inventory
