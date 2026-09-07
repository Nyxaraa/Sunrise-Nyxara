#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../../../state/account/account_state.h"
#include "../../../../state/build_data/socket_entry_lists/definition.h"
#include "../instance/instance_encoder.h"

namespace sunrise::middleware::datagen::family4::loadout {

/** Selector lanes one item instance publishes, one per semantic ability bucket. */
inline constexpr std::size_t kSelectorBucketCount = 12;
/** Selected socket entries per subclass: sprint, class, movement, grenade, super and melee. */
inline constexpr std::size_t kSelectedEntryCount = 6;

/** One selected socket entry and the semantic bucket it publishes into. */
struct SelectedEntry {
    std::uint8_t entry{};
    std::uint8_t bucket{};
};

/** The 6 entries one character has selected on its subclass. */
struct SubclassSelection {
    std::array<SelectedEntry, kSelectedEntryCount> selected{};
};

/**
 * Builds the selection for one subclass item. Only sprint is fixed; grenade, super, melee,
 * movement and class entries are that item's own authored choices, and the class-ability bucket
 * follows the owning character's class.
 * @param item Authored subclass item carrying its own ability choices.
 * @param characterClass Owning character's class, which the class-ability bucket follows.
 * @param output Receives the 6 selected entries.
 */
void subclass_selection(const state::account::inventory::Item& item,
                        state::CharacterClass characterClass,
                        SubclassSelection& output) noexcept;

/**
 * Resolves one item's socket-entry states and selector lanes.
 * Only a list carrying a super lane belongs to a subclass, so every other item keeps its absent
 * and ready states and publishes no selector.
 * @param definition Installed socket-entry-list mapping.
 * @param item Authored item being resolved; only a subclass carries ability choices.
 * @param characterClass Owning character's class, which the class-ability bucket follows.
 * @param acquiredSubclassAbilityMask Owning character's runtime acquired-entry mask.
 * @param output Receives the state of every fixed lane.
 * @param selectors Receives the selector lane of every semantic bucket.
 */
void resolve_socket_states(
    const state::build_data::socket_entry_lists::Definition& definition,
    const state::account::inventory::Item& item,
    state::CharacterClass characterClass,
    std::uint64_t acquiredSubclassAbilityMask,
    std::array<instance::SocketEntryState, instance::layout::kSocketEntryStateCapacity>& output,
    std::array<instance::SocketSelector, kSelectorBucketCount>& selectors) noexcept;

} // namespace sunrise::middleware::datagen::family4::loadout
