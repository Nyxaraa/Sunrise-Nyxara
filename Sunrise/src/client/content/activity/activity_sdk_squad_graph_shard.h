#pragma once

#include <cstdint>

#include "../../../state/build_data/scriptables/definition.h"
#include "activity_sdk_squad_graph.h"

namespace sunrise::client::content::activity::sdk_generation::squad_inventory {

/**
 * Appends only scenario-owned authored-squad graph contexts to one otherwise-built shard.
 * Estate-global definitions deliberately remain outside per-scenario shards.
 */
[[nodiscard]] bool
append_scenario_graph_contexts(std::uint32_t scenarioIndex,
                               const GraphSnapshot& graph,
                               state::build_data::scriptables::Snapshot& output) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::squad_inventory
