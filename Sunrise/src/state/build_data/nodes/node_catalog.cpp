#include "node_catalog.h"

#include <shared_mutex>

#include "../../unlocks/definition.h"
#include "../../unlocks/unlocks_records.h"
#include "../records/definition.h"
#include "../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::nodes {
namespace {

core::threading::SrwLock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;

} // namespace

/** Clears every generated node definition under the catalog lock. */
void clear() noexcept {
    // Drop the catalog lock first: the derived publish reads this catalog under the bank lock.
    {
        const std::lock_guard guard(g_lock);
        g_definitions.clear();
    }
    unlocks::records::republish();
}

/** Checks that the definitions are dense and in native index order. */
bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.empty() || definitions.size() > kDefinitionCapacity) {
        return false;
    }
    for (std::size_t row = 0; row < definitions.size(); ++row) {
        if (definitions[row].definitionIndex != row
            || definitions[row].childCount > kChildCapacity) {
            return false;
        }
    }
    return true;
}

/** Replaces the generated node definitions in one step. */
bool replace(std::span<const Definition> definitions) noexcept {
    if (!valid(definitions)) {
        return false;
    }
    bool replaced = false;
    {
        const std::lock_guard guard(g_lock);
        replaced = g_definitions.replace(definitions);
    }
    if (replaced) {
        unlocks::records::republish();
    }
    return replaced;
}

/** Copies every row in native node order. */
bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.snapshot(output, count);
}

/** @return Number of generated node definitions, read under the lock. */
std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.count();
}

/** Calls back for every node, under the shared lock. */
void for_each(void* context, void (*visit)(void*, const Definition&) noexcept) noexcept {
    const std::shared_lock guard(g_lock);
    for (const Definition& node : g_definitions.rows()) {
        visit(context, node);
    }
}

/** Sets the visibility gate of every lore book category. */
std::size_t apply_visibility(std::span<std::uint8_t> accountFlags) noexcept {
    const std::shared_lock guard(g_lock);
    std::size_t set = 0;
    for (const Definition& node : g_definitions.rows()) {
        if (!node.loreBook || node.visibilityFlagIndex == kUnavailableFlagIndex
            || static_cast<std::size_t>(node.visibilityFlagIndex) >= accountFlags.size()) {
            continue;
        }
        accountFlags[node.visibilityFlagIndex] = unlocks::kFlagSet;
        ++set;
    }
    return set;
}

/** Opens lore categories whose gates read an otherwise-empty value slot. */
std::size_t apply_category_gates(std::span<std::int32_t> objectiveValues) noexcept {
    const std::shared_lock guard(g_lock);
    std::size_t set = 0;
    for (const Definition& node : g_definitions.rows()) {
        if (!node.loreBook) {
            continue;
        }
        // Flag-gated books are handled by apply_visibility.
        if (node.visibilityFlagIndex != kUnavailableFlagIndex
            || node.visibilityCharacterFlagIndex != kUnavailableFlagIndex) {
            continue;
        }
        if (node.valueIndex == kUnavailableValueIndex
            || static_cast<std::size_t>(node.valueIndex) >= objectiveValues.size()) {
            continue;
        }
        // Some node indices alias record objectives and must remain untouched.
        if (node.valueIndex >= records::kObjectiveValueIndexBase) {
            continue;
        }
        // -1 satisfies NOT_ZERO without appearing as one collected chapter on shared bars.
        if (objectiveValues[node.valueIndex] == 0) {
            objectiveValues[node.valueIndex] = -1;
            ++set;
        }
    }
    return set;
}

/** Sets the character scoped visibility gates of the lore book categories. */
std::size_t apply_character_visibility(std::span<std::byte> characterFlags) noexcept {
    const std::shared_lock guard(g_lock);
    std::size_t set = 0;
    for (const Definition& node : g_definitions.rows()) {
        if (!node.loreBook || node.visibilityCharacterFlagIndex == kUnavailableFlagIndex
            || static_cast<std::size_t>(node.visibilityCharacterFlagIndex)
                   >= characterFlags.size()) {
            continue;
        }
        characterFlags[node.visibilityCharacterFlagIndex] =
            static_cast<std::byte>(unlocks::kFlagSet);
        ++set;
    }
    return set;
}

} // namespace sunrise::state::build_data::nodes
