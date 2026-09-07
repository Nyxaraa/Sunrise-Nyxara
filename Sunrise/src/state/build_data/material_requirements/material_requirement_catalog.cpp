#include "material_requirement_catalog.h"

#include <array>
#include <mutex>
#include <shared_mutex>

#include "../table.h"
#include "core/threading/srw_lock.h"

namespace sunrise::state::build_data::material_requirements {
namespace {

core::threading::SrwLock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;

} // namespace

void clear() noexcept {
    const std::lock_guard guard(g_lock);
    g_definitions.clear();
}

/**
 * Checks one complete requirement-set table before it is published.
 * A set owns its index, so a duplicate or out-of-range index rejects the whole table.
 * @param definitions Candidate rows, indexed by requirement-set index.
 * @return True when every set is unique and in range, and its unused rows are clear.
 */
bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.empty() || definitions.size() > kDefinitionCapacity) {
        return false;
    }
    std::array<bool, kDefinitionCapacity> occupied{};
    for (const Definition& definition : definitions) {
        if (definition.requirementSetHash == 0
            || definition.requirementSetIndex >= definitions.size()
            || occupied[definition.requirementSetIndex]
            || definition.requirementCount > definition.requirements.size()) {
            return false;
        }
        for (std::size_t index = 0; index < definition.requirements.size(); ++index) {
            const Requirement& requirement = definition.requirements[index];
            if (index < definition.requirementCount) {
                if (requirement.itemDefinitionIndex == kUnavailableItemDefinitionIndex) {
                    return false;
                }
                for (std::size_t prior = 0; prior < index; ++prior) {
                    if (definition.requirements[prior].itemDefinitionIndex
                        == requirement.itemDefinitionIndex) {
                        return false;
                    }
                }
            } else if (requirement.itemDefinitionIndex != kUnavailableItemDefinitionIndex
                       || requirement.quantity != 0
                       || requirement.condition != kUnconditionalRequirement
                       || requirement.deleteOnAction || requirement.omitFromRequirements) {
                return false;
            }
        }
        occupied[definition.requirementSetIndex] = true;
    }
    return true;
}

/**
 * Validates one table and publishes it, placing each set at its own index.
 * @param definitions Candidate rows, rejected as a whole when any row is invalid.
 * @return True when the table replaced the published one.
 */
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
        storage[definition.requirementSetIndex] = definition;
    }
    return true;
}

bool find(std::uint16_t requirementSetIndex, Definition& definition) noexcept {
    definition = {};
    const std::shared_lock guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    const bool found = static_cast<std::size_t>(requirementSetIndex) < rows.size();
    if (found) {
        definition = rows[requirementSetIndex];
    }
    return found;
}

bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.snapshot(output, count);
}

std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.count();
}

} // namespace sunrise::state::build_data::material_requirements
