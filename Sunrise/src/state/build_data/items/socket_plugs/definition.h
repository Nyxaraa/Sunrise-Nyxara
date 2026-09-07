#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../details/definition.h"

namespace sunrise::state::build_data::items::socket_plugs {

/** Enhanced Sword Scavenger already carries the correct Arrivals leg-armour socket relation. */
inline constexpr std::uint32_t kArrivalsLegReferenceHash = 3'213'968'579U;
/** Plug category declared by Enhanced Sword Scavenger and required by leg-armour sockets. */
inline constexpr std::uint32_t kArrivalsLegCategoryHash = 0x7DDE0206U;
/** Arrivals artifact records whose leg-armour label conflicts with their shipped general pool. */
inline constexpr std::array<std::uint32_t, 4> kArrivalsLegModHashes{
    3'465'659'109U, // Flourishing Blade
    3'465'659'111U, // Automatic Prize
    3'465'659'104U, // Dimensional Tithes
    3'465'659'105U, // Ascendant Bounty
};

/** Ordinary item instances expose at most 12 socket lanes. */
inline constexpr std::size_t kLaneCapacity = details::kInitialPlugCapacity;
/** At most one exact pool rule is retained for each installed item and ordinary socket lane. */
inline constexpr std::size_t kRuleCapacity = details::kDefinitionCapacity * kLaneCapacity;
/** Pool zero is the shared empty pool, in addition to at most one unique pool per rule. */
inline constexpr std::size_t kPoolCapacity = kRuleCapacity + 1;
/** Four million 16-bit members bound the deduplicated installed-build relation to 8 MiB. */
inline constexpr std::size_t kMemberCapacity = 1U << 22U;
/** Pool zero is always the canonical empty pool. */
inline constexpr std::uint32_t kEmptyPoolIndex = 0;

/** One installed item socket and the exact deduplicated plug pool it accepts. */
struct Rule {
    std::uint16_t itemDefinitionIndex{};
    std::uint8_t lane{};
    /** Must remain zero so the runtime and packed forms are deterministic. */
    std::uint8_t reserved{};
    std::uint32_t poolIndex{};
};

/** One contiguous range in the flat, sorted plug-definition index bank. */
struct Pool {
    std::uint32_t memberOffset{};
    std::uint32_t memberCount{};
};

/** Native item-definition index of one allowed plug. */
using Member = std::uint16_t;

/** Called once per pool member; returning false stops the walk. */
using MemberVisitor = bool (*)(void* context, Member plugDefinitionIndex) noexcept;

} // namespace sunrise::state::build_data::items::socket_plugs
