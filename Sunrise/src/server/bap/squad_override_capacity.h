#pragma once

#include <cstdint>

namespace sunrise::state::build_data::scenarios {
struct Definition;
struct RosterGroup;
} // namespace sunrise::state::build_data::scenarios

namespace sunrise::server::bap {

struct SquadOverrideLease;

namespace squad_override_capacity {

/**
 * Checks the ClientRef manager budget used by one squad admission.
 * A state-local request uses its authored target bubble even when the player reports another one;
 * a canonical request uses the selected player region instead.
 * @param layout Canonical top-level and per-bubble groups published by the destination.
 * @param lease Retained generated groups, or null before the first override.
 * @param pendingGroup Newly published generated group, or null for an existing group.
 * @param stateLocalTarget True when pendingGroup belongs to an authored bubble sub-block.
 * @param targetRegion Authored slice-set index for a state-local target.
 * @param selectedRegion Slice-set index currently selected for the player.
 * @return True when top-level plus the chosen bubble fits 128 registrations and 3,072 records.
 */
[[nodiscard]] bool available(const state::build_data::scenarios::Definition& layout,
                             const SquadOverrideLease* lease,
                             const state::build_data::scenarios::RosterGroup* pendingGroup,
                             bool stateLocalTarget,
                             std::int32_t targetRegion,
                             std::int32_t selectedRegion) noexcept;

} // namespace squad_override_capacity
} // namespace sunrise::server::bap
