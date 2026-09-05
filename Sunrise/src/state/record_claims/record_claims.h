#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sunrise::state::record_claims {

/** Result of advancing a single-objective record. */
enum class ObjectiveAdvance : std::uint8_t {
    unavailable,
    alreadyHeld,
    advanced,
    completed,
};

/** Claim overlaid while its item reward and outbound account image are staged. */
struct PendingClaim {
    std::uint16_t flagIndex{};
    std::uint16_t scoreValue{};
};

/** Loads persisted claim, claimable, and partial-progress state. */
[[nodiscard]] bool initialize(void* module) noexcept;

/** Persists a first claim; duplicates return false and cannot replay rewards. */
[[nodiscard]] bool claim(std::uint16_t flagIndex, std::uint16_t scoreValue) noexcept;

/** Redeems the next completed milestone of a flagless interval Triumph. */
[[nodiscard]] bool claim_interval(std::uint16_t recordIndex,
                                  std::uint32_t definitionHash) noexcept;

/** Persists a first complete-but-unclaimed record. */
[[nodiscard]] bool mark_claimable(std::uint16_t flagIndex) noexcept;

/** Advances and persists one objective, promoting it to claimable at its threshold. */
[[nodiscard]] ObjectiveAdvance advance_single_objective(std::uint16_t flagIndex) noexcept;

/** @return True when this index is complete but unclaimed. */
[[nodiscard]] bool claimable(std::uint16_t flagIndex) noexcept;

/** Applies one coherent claims snapshot to the account flags, score, bars, and objectives. */
void apply_account_projection(std::span<std::uint8_t> accountFlags,
                              std::span<std::int32_t> objectiveValues,
                              const PendingClaim* pending = nullptr) noexcept;

/** @return True when this index is already held. */
[[nodiscard]] bool claimed(std::uint16_t flagIndex) noexcept;

/** Publishes character-scoped lore-book progress bars. */
std::size_t apply_character_node_progress(std::span<std::int32_t> characterValues,
                                          const PendingClaim* pending = nullptr) noexcept;

/** @return Total score of every held claim. */
[[nodiscard]] std::uint32_t total_score() noexcept;

/** @return Number of distinct indices held. */
[[nodiscard]] std::size_t count() noexcept;

/** Invalidates projections derived from replaceable build-data catalogs. */
void invalidate_build_data_cache() noexcept;

} // namespace sunrise::state::record_claims
