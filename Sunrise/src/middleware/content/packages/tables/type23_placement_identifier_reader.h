#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "slot_descriptor_reader.h"

namespace sunrise::middleware::content::packages::tables {

/** Component class whose descriptor carries the three-channel type-23 state. */
inline constexpr std::uint32_t kType23ComponentClass = 0x80804F45U;
/** Exact offset of its opaque placement identifier inside the descriptor. */
inline constexpr std::size_t kType23PlacementIdentifierOffset = 0x58;

/**
 * Reads the class-specific opaque identifier used for an exact package equality join.
 * This function deliberately does not assign runtime semantics to the identifier.
 */
[[nodiscard]] bool type23_placement_identifier(std::span<const std::byte> blob,
                                               const SlotDescriptor& descriptor,
                                               std::uint64_t& output) noexcept;

} // namespace sunrise::middleware::content::packages::tables
