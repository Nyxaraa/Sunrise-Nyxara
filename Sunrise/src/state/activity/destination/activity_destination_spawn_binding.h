#pragma once

#include <cstdint>

#include "definition.h"

namespace sunrise::state::activity::destination {

/**
 * Picks the spawn-set hash to send, preserving explicit authored/operator overrides.
 * Inferred hashes are filtered against the known map/direct activity packages. The list is not
 * a transitive dependency inventory, so a missing package must not veto an explicit override.
 * @param selection Committed destination.
 * @param fallback Authored fallback hash.
 * @return The explicit override, or the inferred hash after package filtering.
 */
[[nodiscard]] std::uint32_t attachable_spawn_set_hash(const DestinationSelection& selection,
                                                      std::uint32_t fallback) noexcept;

/**
 * Finds the slice set the type-17 spawn override must name for one spawn-set hash.
 * The override is a pair, and the Client searches for the hash inside the slice set the pair
 * names. Naming the arrival works only while the arrival bubble is one the set is declared in;
 * anywhere else the search finds nothing and the player never receives a spawn point. A set the
 * catalog cannot place leaves the arrival standing, which is the previous behaviour.
 * @param selection Committed destination.
 * @param spawnSetHash Hash the override will carry.
 * @param arrivalSliceSet Slice set the destination arrives in.
 * @return The arrival when it already declares the set, otherwise the set's own slice set.
 */
[[nodiscard]] std::uint16_t spawn_set_slice_set(const DestinationSelection& selection,
                                                std::uint32_t spawnSetHash,
                                                std::uint16_t arrivalSliceSet) noexcept;

} // namespace sunrise::state::activity::destination
