#include "collectible_catalog.h"

#include <array>
#include <shared_mutex>

#include "../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::collectibles {
namespace {

core::threading::SrwLock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;

} // namespace

/** Clears the table while no reader can observe a partial replacement. */
void clear() noexcept {
    const std::lock_guard guard(g_lock);
    g_definitions.clear();
}

/** Checks that the native indices cover one complete dense range, in any input order. */
bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.empty() || definitions.size() > kDefinitionCapacity) {
        return false;
    }
    std::array<bool, kDefinitionCapacity> occupied{};
    for (const Definition& definition : definitions) {
        if (definition.collectibleIndex >= definitions.size()
            || occupied[definition.collectibleIndex]
            || definition.materialRequirementCount > definition.materialRequirements.size()) {
            return false;
        }
        const bool hasRequirements = definition.materialRequirementCount != 0;
        if (hasRequirements
                != (definition.materialRequirementSetIndex
                    != kUnavailableMaterialRequirementSetIndex)
            || hasRequirements != (definition.materialRequirementSetHash != 0)) {
            return false;
        }
        for (std::size_t index = 0; index < definition.materialRequirements.size(); ++index) {
            const MaterialRequirement& requirement = definition.materialRequirements[index];
            if (index < definition.materialRequirementCount) {
                if (requirement.itemDefinitionIndex == kUnavailableItemDefinitionIndex
                    || requirement.quantity == 0) {
                    return false;
                }
                for (std::size_t prior = 0; prior < index; ++prior) {
                    if (definition.materialRequirements[prior].itemDefinitionIndex
                        == requirement.itemDefinitionIndex) {
                        return false;
                    }
                }
            } else if (requirement.itemDefinitionIndex != kUnavailableItemDefinitionIndex
                       || requirement.quantity != 0 || requirement.deleteOnAction
                       || requirement.omitFromRequirements) {
                return false;
            }
        }
        occupied[definition.collectibleIndex] = true;
    }
    return true;
}

/** Places every validated row at the native index the request protocol uses. */
bool replace(std::span<const Definition> definitions) noexcept {
    if (!valid(definitions)) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    const std::span<Definition> storage = g_definitions.reset(definitions.size());
    if (storage.size() != definitions.size()) {
        return false;
    }
    for (const Definition& definition : definitions) {
        storage[definition.collectibleIndex] = definition;
    }
    return true;
}

/** Finds a row only when it is inside the published dense table. */
bool find(std::uint16_t collectibleIndex, Definition& definition) noexcept {
    definition = {};
    definition.itemDefinitionIndex = kUnavailableItemDefinitionIndex;
    const std::shared_lock guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    const bool found = static_cast<std::size_t>(collectibleIndex) < rows.size();
    if (found) {
        definition = rows[collectibleIndex];
    }
    return found;
}

/** Answers whether any published collectible grants one installed item row. */
bool grants_item(std::uint16_t itemDefinitionIndex) noexcept {
    if (itemDefinitionIndex == kUnavailableItemDefinitionIndex) {
        return false;
    }
    const std::shared_lock guard(g_lock);
    for (const Definition& definition : g_definitions.rows()) {
        if (definition.itemDefinitionIndex == itemDefinitionIndex) {
            return true;
        }
    }
    return false;
}

/** Finds the collectible that grants one installed item row. */
bool find_granting(std::uint16_t itemDefinitionIndex, std::uint16_t& collectibleIndex) noexcept {
    if (itemDefinitionIndex == kUnavailableItemDefinitionIndex) {
        return false;
    }
    const std::shared_lock guard(g_lock);
    for (const Definition& definition : g_definitions.rows()) {
        if (definition.itemDefinitionIndex == itemDefinitionIndex) {
            collectibleIndex = definition.collectibleIndex;
            return true;
        }
    }
    return false;
}

/** Copies the dense rows without exposing catalog storage. */
bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.snapshot(output, count);
}

/** @return Number of published rows, read under the catalog lock. */
std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.count();
}

} // namespace sunrise::state::build_data::collectibles
