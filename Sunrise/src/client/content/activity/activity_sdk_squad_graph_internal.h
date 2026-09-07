#pragma once

#include "activity_sdk_squad_graph.h"

namespace sunrise::client::content::activity::sdk_generation::squad_inventory::detail {

/** Appends descriptor/reference/edge joins after all definition and context rows exist. */
[[nodiscard]] bool build_graph_edges(const topology_inventory::Snapshot& topology,
                                     const Facts& facts,
                                     GraphSnapshot& output);

} // namespace sunrise::client::content::activity::sdk_generation::squad_inventory::detail
