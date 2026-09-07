/**
 * Subclass socket selection. An item's socket entries start absent or ready.
 * The entries the character has selected make their whole group active. The super lane is
 * active too, although it carries no plug source.
 */

#include "subclass_socket_selection.h"

#include "../../../../state/build_data/runtime.h"

namespace sunrise::middleware::datagen::family4::loadout {
namespace {

namespace build_socket_lists = state::build_data::socket_entry_lists;

/** Socket entry of the sprint ability. */
constexpr std::uint8_t kSprintEntry = 1;

} // namespace

/** Builds the selection for one subclass item. */
void subclass_selection(const state::account::inventory::Item& item,
                        state::CharacterClass characterClass,
                        SubclassSelection& output) noexcept {
    output = {};
    output.selected[0] = {item.grenadeAbilityEntry, state::kGrenadeAbilityBucket};
    output.selected[1] = {item.superAbilityEntry, state::kSuperAbilityBucket};
    output.selected[2] = {item.meleeAbilityEntry, state::kMeleeAbilityBucket};
    output.selected[3] = {item.movementAbilityEntry, state::kMovementAbilityBucket};
    output.selected[4] = {kSprintEntry, state::kSprintAbilityBucket};
    output.selected[5] = {item.classAbilityEntry, state::class_ability_bucket(characterClass)};
}

/** Resolves one item's socket-entry states and selector lanes. */
void resolve_socket_states(
    const build_socket_lists::Definition& definition,
    const state::account::inventory::Item& item,
    state::CharacterClass characterClass,
    std::uint64_t acquiredSubclassAbilityMask,
    std::array<instance::SocketEntryState, instance::layout::kSocketEntryStateCapacity>& output,
    std::array<instance::SocketSelector, kSelectorBucketCount>& selectors) noexcept {
    output.fill(instance::SocketEntryState::absent);
    selectors.fill(instance::SocketSelector{});
    // Only a subclass keeps an entry table, so this lookup is what identifies one.
    build_socket_lists::EntryTable entries{};
    const bool subclass =
        state::build_data::find_socket_entry_table(definition.definitionIndex, entries);
    for (std::size_t index = 0; index < definition.entryCount; ++index) {
        const std::uint64_t bit = std::uint64_t{1} << index;
        if ((definition.readyMask & bit) != 0) {
            // The mask names subclass ability entries, so an ordinary item's lane stays ready.
            // It defaults to all set, and a lane past bit 63 would read as clear, so promoting
            // every item here made ready unreachable for all 36 entries.
            output[index] = subclass && (acquiredSubclassAbilityMask & bit) != 0
                                ? instance::SocketEntryState::acquired
                                : instance::SocketEntryState::ready;
        }
    }
    if (!subclass) {
        return;
    }
    SubclassSelection selection{};
    subclass_selection(item, characterClass, selection);

    // A group of 2 or 3 entries is mutually exclusive alternatives: exactly one lights up. An
    // Attunement's group packs several 4-node options into one group id, so a population past the
    // widest single bundle means its members activate in same-sized runs.
    std::array<std::uint16_t, build_socket_lists::kEntryCapacity> groupPopulation{};
    for (std::size_t index = 0; index < definition.entryCount; ++index) {
        const std::uint8_t group = entries.entries[index].group;
        if (group < groupPopulation.size()) {
            ++groupPopulation[group];
        }
    }
    // Each selected entry claims its group. Every entry sharing that group and plug source is
    // active too, which is why a run of duplicate lanes flips together.
    std::array<std::uint32_t, build_socket_lists::kEntryCapacity> chosen{};
    std::array<bool, build_socket_lists::kEntryCapacity> claimed{};
    std::array<bool, build_socket_lists::kEntryCapacity> forcedActive{};
    for (const SelectedEntry& selected : selection.selected) {
        if (selected.entry >= definition.entryCount || selected.bucket >= selectors.size()) {
            continue;
        }
        selectors[selected.bucket] = instance::SocketSelector{selected.entry, 0, 0};
        const build_socket_lists::Entry& entry = entries.entries[selected.entry];
        if (entry.plugSource == build_socket_lists::kNoPlugSource || entry.group >= claimed.size()
            || claimed[entry.group]) {
            continue;
        }
        claimed[entry.group] = true;
        chosen[entry.group] = entry.plugSource;
        if (groupPopulation[entry.group] <= state::kMaxAttunementBundleSize) {
            continue;
        }
        // A pick can bundle several consecutive entries under the same group, all publishing
        // together. Siblings carry their own plug source, so force the whole contiguous run
        // active rather than relying on the plug-source match below.
        forcedActive[selected.entry] = true;
        for (std::size_t offset = 1;
             offset < state::kMaxAttunementBundleSize
             && selected.entry + offset < definition.entryCount
             && entries.entries[selected.entry + offset].group == entry.group;
             ++offset) {
            forcedActive[selected.entry + offset] = true;
        }
    }
    for (std::size_t index = 0; index < definition.entryCount; ++index) {
        const build_socket_lists::Entry& entry = entries.entries[index];
        const bool matchesGroup = entry.plugSource != build_socket_lists::kNoPlugSource
                                  && entry.group < claimed.size() && claimed[entry.group]
                                  && chosen[entry.group] == entry.plugSource;
        const bool superLane = entry.plugSource == build_socket_lists::kNoPlugSource
                               && entry.kind == build_socket_lists::kSuperEntryKind;
        if (matchesGroup || superLane || forcedActive[index]) {
            output[index] = instance::SocketEntryState::active;
        }
    }
}

} // namespace sunrise::middleware::datagen::family4::loadout
