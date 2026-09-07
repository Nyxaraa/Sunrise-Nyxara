#include "mission_script_sdk_bridge.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../activity_sdk_mission_runtime.h"
#include "mission_script_catalog_sdk_bridge.h"
#include "mission_script_message_catalog.h"

namespace sunrise::server::activity::mission::sdk_bridge {
namespace {

namespace sdk = state::activity_sdk;
namespace format = state::activity_sdk::format;
namespace scenes = activity_sdk_mission;

// Every occurrence text id starts with this family prefix.
constexpr std::string_view kOccurrencePrefix = "object-occurrence/";

[[nodiscard]] const sdk::BoundView* context_view(const void* context) noexcept {
    return static_cast<const sdk::BoundView*>(context);
}

[[nodiscard]] bool valid_view(const sdk::BoundView* view) noexcept {
    return view != nullptr && view->catalog != nullptr && sdk::bound_activity(*view) != nullptr
           && sdk::bound_scenario(*view) != nullptr;
}

[[nodiscard]] const format::Activity* bound_activity(const sdk::BoundView* view) noexcept {
    return view != nullptr && view->catalog != nullptr ? sdk::bound_activity(*view) : nullptr;
}

/** Copies only authenticated pack fields; no mapped pointer is exposed to Lua. */
[[nodiscard]] bool resolve_activity_binding(const void* context,
                                            lua_vm::ActivityBindingDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    const format::Activity* const activity = bound_activity(view);
    return activity != nullptr && activity_binding_definition(*view->catalog, *activity, output);
}

[[nodiscard]] std::size_t activity_binding_tag_count(const void* context,
                                                     lua_vm::ActivityBindingTagKind kind) noexcept {
    const sdk::BoundView* const view = context_view(context);
    const format::Activity* const activity = bound_activity(view);
    return activity != nullptr ? activity_binding_tags(*view->catalog, *activity, kind).size() : 0;
}

/** Resolves the package tag of the activity binding the view names. */
[[nodiscard]] bool resolve_activity_binding_tag(const void* context,
                                                lua_vm::ActivityBindingTagKind kind,
                                                std::uint32_t localRow,
                                                std::uint32_t& output) noexcept {
    output = 0;
    const sdk::BoundView* const view = context_view(context);
    if (view == nullptr || view->catalog == nullptr || localRow == 0) {
        return false;
    }
    const format::Activity* const activity = sdk::bound_activity(*view);
    if (activity == nullptr) {
        return false;
    }
    const auto rows = activity_binding_tags(*view->catalog, *activity, kind);
    if (localRow > rows.size()) {
        return false;
    }
    output = rows[localRow - 1U].tag;
    return output != 0;
}

[[nodiscard]] std::span<const format::ActivityBindingLocator>
binding_locators(const sdk::BoundView& view) noexcept {
    const format::Activity* const activity = sdk::bound_activity(view);
    return activity != nullptr ? sdk::activity_binding_locators(*view.catalog, *activity)
                               : std::span<const format::ActivityBindingLocator>{};
}

[[nodiscard]] std::size_t activity_binding_locator_count(const void* context) noexcept {
    const sdk::BoundView* const view = context_view(context);
    return view != nullptr && view->catalog != nullptr ? binding_locators(*view).size() : 0;
}

/** Copies one 1-based binding locator of the bound activity. @return False when absent. */
[[nodiscard]] bool
resolve_activity_binding_locator(const void* context,
                                 std::uint32_t localRow,
                                 lua_vm::ActivityBindingLocatorDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    if (view == nullptr || view->catalog == nullptr || localRow == 0) {
        return false;
    }
    const auto rows = binding_locators(*view);
    if (localRow > rows.size()) {
        return false;
    }
    const format::ActivityBindingLocator& row = rows[localRow - 1U];
    output = {.tag = row.tag, .offset = row.offset, .localRow = localRow};
    return row.tag != 0;
}

/** Rejects incomplete squad rows so Lua never receives a partly usable definition. */
[[nodiscard]] bool squad_definition(const sdk::BoundView& view,
                                    const format::Squad& squad,
                                    std::uint32_t localRow,
                                    lua_vm::SquadDefinition& output) noexcept {
    output = {};
    const sdk::Catalog& catalog = *view.catalog;
    const auto allSquads = catalog.squads();
    const auto members = sdk::squad_members(catalog, squad);
    if (members.empty() || members.size() > output.defaultCounts.size()
        || (squad.flags & format::kSquadRunnableMask) != format::kSquadRunnableMask
        || &squad < allSquads.data() || &squad >= allSquads.data() + allSquads.size()) {
        return false;
    }
    for (std::size_t index = 0; index < members.size(); ++index) {
        output.defaultCounts[index] = members[index].defaultCount;
    }
    output.id = catalog.string(squad.id);
    const auto slots = catalog.slots();
    if (squad.slotIndex >= slots.size()) {
        return false;
    }
    output.name = catalog.string(slots[squad.slotIndex].name);
    output.nativeRow = static_cast<std::uint32_t>(&squad - allSquads.data());
    output.localRow = localRow;
    output.memberCount = members.size();
    return !output.id.empty();
}

/** Treats Lua squad rows as one-based positions inside the bound scenario. */
[[nodiscard]] bool resolve_squad_row(const void* context,
                                     std::uint32_t localRow,
                                     lua_vm::SquadDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view) || localRow == 0) {
        return false;
    }
    const format::Scenario* const scenario = sdk::bound_scenario(*view);
    const auto squads = sdk::scenario_squads(*view->catalog, *scenario);
    return localRow <= squads.size()
           && squad_definition(*view, squads[localRow - 1], localRow, output);
}

/** Refuses squad names and aliases that do not resolve to one runnable squad. */
[[nodiscard]] bool resolve_squad_id(const void* context,
                                    std::string_view id,
                                    lua_vm::SquadDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view) || id.empty()) {
        return false;
    }
    const format::Scenario* const scenario = sdk::bound_scenario(*view);
    const auto squads = sdk::scenario_squads(*view->catalog, *scenario);
    std::size_t matches = 0;
    lua_vm::SquadDefinition selected{};
    for (std::size_t index = 0; index < squads.size(); ++index) {
        const auto slots = view->catalog->slots();
        if (squads[index].slotIndex >= slots.size()) {
            return false;
        }
        const format::Slot& slot = slots[squads[index].slotIndex];
        bool names = view->catalog->string(squads[index].id) == id
                     || view->catalog->string(slot.id) == id
                     || view->catalog->string(slot.name) == id;
        for (const format::Text& alias : sdk::slot_aliases(*view->catalog, slot)) {
            names = names || view->catalog->string(alias.value) == id;
        }
        if (!names) {
            continue;
        }
        lua_vm::SquadDefinition candidate{};
        if (!squad_definition(
                *view, squads[index], static_cast<std::uint32_t>(index + 1), candidate)) {
            continue;
        }
        selected = candidate;
        ++matches;
    }
    if (matches != 1) {
        return false;
    }
    output = selected;
    return true;
}

[[nodiscard]] bool object_seen_before(std::span<const format::Occurrence> occurrences,
                                      std::size_t selected) noexcept {
    for (std::size_t index = 0; index < selected; ++index) {
        if (occurrences[index].objectIndex == occurrences[selected].objectIndex) {
            return true;
        }
    }
    return false;
}

/** Rejects cross-table rows before exposing stable native indices to Lua. */
[[nodiscard]] bool slot_definition(const sdk::BoundView& view,
                                   const format::Object& object,
                                   const format::Slot& slot,
                                   std::uint32_t localRow,
                                   lua_vm::SlotDefinition& output) noexcept {
    output = {};
    const sdk::Catalog& catalog = *view.catalog;
    const auto allObjects = catalog.objects();
    const auto allSlots = catalog.slots();
    if (&object < allObjects.data() || &object >= allObjects.data() + allObjects.size()
        || &slot < allSlots.data() || &slot >= allSlots.data() + allSlots.size()
        || slot.objectIndex != static_cast<std::uint32_t>(&object - allObjects.data())) {
        return false;
    }
    output.id = catalog.string(slot.id);
    output.name = catalog.string(slot.name);
    output.objectId = catalog.string(object.id);
    output.senseSchemaId = catalog.string(slot.senseSchemaId);
    output.authSchemaId = catalog.string(slot.authSchemaId);
    output.nativeRow = static_cast<std::uint32_t>(&slot - allSlots.data());
    output.localRow = localRow;
    output.objectTag = object.objectTag;
    output.registryKey = object.objectKey;
    output.slotIndex = slot.slotIndex;
    output.slotType = slot.slotType;
    output.componentClass = slot.componentClass;
    output.senseSchema = slot.senseSchema;
    output.authSchema = slot.authSchema;
    output.flags = slot.flags;
    return !output.id.empty() && !output.objectId.empty();
}

/** Deduplicates repeated object occurrences and requires one selected slot. */
template <typename Select>
[[nodiscard]] bool
resolve_slot(const sdk::BoundView& view, Select&& select, lua_vm::SlotDefinition& output) noexcept {
    output = {};
    if (!valid_view(&view)) {
        return false;
    }
    const sdk::Catalog& catalog = *view.catalog;
    const format::Scenario* const scenario = sdk::bound_scenario(view);
    const auto occurrences = sdk::scenario_occurrences(catalog, *scenario);
    const auto objects = catalog.objects();
    std::uint32_t localRow = 0;
    std::size_t matches = 0;
    lua_vm::SlotDefinition selected{};
    for (std::size_t occurrenceIndex = 0; occurrenceIndex < occurrences.size(); ++occurrenceIndex) {
        const format::Occurrence& occurrence = occurrences[occurrenceIndex];
        if (occurrence.objectIndex >= objects.size()) {
            return false;
        }
        if (object_seen_before(occurrences, occurrenceIndex)) {
            continue;
        }
        const format::Object& object = objects[occurrence.objectIndex];
        for (const format::Slot& slot : sdk::object_slots(catalog, object)) {
            if (localRow == (std::numeric_limits<std::uint32_t>::max)()) {
                return false;
            }
            ++localRow;
            lua_vm::SlotDefinition candidate{};
            if (!slot_definition(view, object, slot, localRow, candidate)) {
                return false;
            }
            if (!select(candidate)) {
                continue;
            }
            selected = candidate;
            ++matches;
        }
    }
    if (matches != 1) {
        return false;
    }
    output = selected;
    return true;
}

/** Treats Lua slot rows as one-based positions in the deduplicated scenario view. */
[[nodiscard]] bool resolve_slot_row(const void* context,
                                    std::uint32_t localRow,
                                    lua_vm::SlotDefinition& output) noexcept {
    const sdk::BoundView* const view = context_view(context);
    return valid_view(view) && localRow != 0
           && resolve_slot(
               *view,
               [localRow](const lua_vm::SlotDefinition& value) noexcept {
                   return value.localRow == localRow;
               },
               output);
}

/** Requires one exact slot ID, name, or alias in the bound scenario. */
[[nodiscard]] bool
resolve_slot_id(const void* context, std::string_view id, lua_vm::SlotDefinition& output) noexcept {
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view) || id.empty()) {
        return false;
    }
    return resolve_slot(
        *view,
        [view, id](const lua_vm::SlotDefinition& value) noexcept {
            if (value.id == id || value.name == id) {
                return true;
            }
            const format::Slot& slot = view->catalog->slots()[value.nativeRow];
            for (const format::Text& alias : sdk::slot_aliases(*view->catalog, slot)) {
                if (view->catalog->string(alias.value) == id) {
                    return true;
                }
            }
            return false;
        },
        output);
}

/** Resolves sensor keys only when every authored slot identity field agrees. */
[[nodiscard]] bool resolve_sense_slot(const void* context,
                                      const host::SenseObservationKey& key,
                                      lua_vm::SlotDefinition& output) noexcept {
    const sdk::BoundView* const view = context_view(context);
    return valid_view(view) && key.senseSchema != 0
           && resolve_slot(
               *view,
               [&key](const lua_vm::SlotDefinition& value) noexcept {
                   return value.registryKey == key.registryKey && value.objectTag == key.objectTag
                          && value.slotIndex == key.slotIndex && value.slotType == key.slotType
                          && value.senseSchema == key.senseSchema;
               },
               output);
}

/** @return True when a catalog-global slot belongs to the bound scenario. */
[[nodiscard]] bool scenario_has_slot(const sdk::BoundView& view, std::uint32_t slotRow) noexcept {
    if (!valid_view(&view) || slotRow >= view.catalog->slots().size()) {
        return false;
    }
    const sdk::Catalog& catalog = *view.catalog;
    const auto objects = catalog.objects();
    const format::Slot& selected = catalog.slots()[slotRow];
    for (const format::Occurrence& occurrence :
         sdk::scenario_occurrences(catalog, *sdk::bound_scenario(view))) {
        if (occurrence.objectIndex == selected.objectIndex
            && occurrence.objectIndex < objects.size()) {
            return true;
        }
    }
    return false;
}

/** Copies one exact generated task target into its value-owned VM shape. */
[[nodiscard]] bool task_definition(const sdk::BoundView& view,
                                   const format::TaskTarget& row,
                                   std::uint32_t localRow,
                                   lua_vm::TaskSensorDefinition& output) noexcept {
    output = {};
    const format::Slot* const objective = sdk::task_linked_objective_slot(*view.catalog, row);
    if (objective == nullptr || !scenario_has_slot(view, row.taskSlotIndex)
        || !scenario_has_slot(view, row.objectiveSlotIndex)) {
        return false;
    }
    output.id = view.catalog->string(row.id);
    output.localRow = localRow;
    output.slotRow = row.taskSlotIndex;
    output.targetObjectKey = row.targetObjectKey;
    output.bitIndex = row.bitIndex;
    return !output.id.empty();
}

/** Treats task rows as one-based positions inside the bound scenario. */
[[nodiscard]] bool resolve_task_sensor_row(const void* context,
                                           std::uint32_t localRow,
                                           lua_vm::TaskSensorDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view) || localRow == 0) {
        return false;
    }
    std::uint32_t current = 0;
    for (const format::TaskTarget& row : view->catalog->task_targets()) {
        if (!scenario_has_slot(*view, row.taskSlotIndex)) {
            continue;
        }
        ++current;
        if (current == localRow) {
            return task_definition(*view, row, current, output);
        }
    }
    return false;
}

/** Resolves one generated task ID without accepting an ambiguous alias. */
[[nodiscard]] bool resolve_task_sensor_id(const void* context,
                                          std::string_view id,
                                          lua_vm::TaskSensorDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view) || id.empty()) {
        return false;
    }
    std::uint32_t current = 0;
    std::size_t matches = 0;
    lua_vm::TaskSensorDefinition selected{};
    for (const format::TaskTarget& row : view->catalog->task_targets()) {
        if (!scenario_has_slot(*view, row.taskSlotIndex)) {
            continue;
        }
        ++current;
        if (view->catalog->string(row.id) == id) {
            lua_vm::TaskSensorDefinition candidate{};
            if (task_definition(*view, row, current, candidate)) {
                selected = candidate;
                ++matches;
            }
        }
    }
    if (matches != 1) {
        return false;
    }
    output = selected;
    return true;
}

/** Copies one generated type-68 element into its crash-safe VM shape. */
[[nodiscard]] bool directive_definition(const sdk::BoundView& view,
                                        const format::DirectiveElement& row,
                                        std::uint32_t localRow,
                                        lua_vm::DirectiveElementDefinition& output) noexcept {
    output = {};
    if (!scenario_has_slot(view, row.slotIndex) || row.nameHash == 0
        || row.nameHash == format::kAbsentIndex || row.elementIndex < 0
        || static_cast<std::uint32_t>(row.elementIndex) >= row.elementCount) {
        return false;
    }
    output.id = view.catalog->string(row.id);
    output.localRow = localRow;
    output.slotRow = row.slotIndex;
    output.nameHash = row.nameHash;
    output.elementIndex = row.elementIndex;
    output.elementCount = row.elementCount;
    return !output.id.empty();
}

/** Treats directive rows as one-based positions inside the bound scenario. */
[[nodiscard]] bool
resolve_directive_element_row(const void* context,
                              std::uint32_t localRow,
                              lua_vm::DirectiveElementDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view) || localRow == 0) {
        return false;
    }
    std::uint32_t current = 0;
    for (const format::DirectiveElement& row : view->catalog->directive_elements()) {
        if (!scenario_has_slot(*view, row.slotIndex)) {
            continue;
        }
        ++current;
        if (current == localRow) {
            return directive_definition(*view, row, current, output);
        }
    }
    return false;
}

/** Resolves the exact type-68 slot/hash/index triple before the native dereference. */
[[nodiscard]] bool resolve_directive_element(const void* context,
                                             std::uint32_t slotRow,
                                             std::uint32_t nameHash,
                                             std::int32_t elementIndex,
                                             lua_vm::DirectiveElementDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view)) {
        return false;
    }
    std::uint32_t current = 0;
    std::size_t matches = 0;
    lua_vm::DirectiveElementDefinition selected{};
    for (const format::DirectiveElement& row : view->catalog->directive_elements()) {
        if (!scenario_has_slot(*view, row.slotIndex)) {
            continue;
        }
        ++current;
        if (row.slotIndex == slotRow && row.nameHash == nameHash
            && row.elementIndex == elementIndex) {
            lua_vm::DirectiveElementDefinition candidate{};
            if (directive_definition(*view, row, current, candidate)) {
                selected = candidate;
                ++matches;
            }
        }
    }
    if (matches != 1) {
        return false;
    }
    output = selected;
    return true;
}

/** Distinct state names the largest guard actor declares fit here with room to spare. */
inline constexpr std::size_t kPerformanceStateCapacity = 256;

/**
 * Resolves one state name on the squad a type-42 sensor drives. A zero hash selects the single
 * name the target declares. A given hash must be declared by every member that declares names.
 */
[[nodiscard]] bool resolve_performance_state(const void* context,
                                             std::uint32_t slotRow,
                                             std::uint32_t nameHash,
                                             lua_vm::PerformanceStateDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view) || !scenario_has_slot(*view, slotRow)) {
        return false;
    }
    const sdk::Catalog& catalog = *view->catalog;
    const format::Slot& slot = catalog.slots()[slotRow];
    if (slot.slotType != format::kPerformanceSlotType
        || slot.componentClass != format::kPerformanceComponentClass
        || slot.authSchema != format::kPerformanceAuthSchema
        || (slot.flags & format::kSlotSchemaJoinExact) == 0) {
        return false;
    }
    std::uint32_t squadSlot = format::kAbsentIndex;
    std::size_t edges = 0;
    for (const format::AuthoredSceneSquadEdge& edge :
         sdk::slot_authored_scene_squad_edges(catalog, slot)) {
        if ((edge.flags & format::kAuthoredSceneSquadPerformanceTargetExact) != 0) {
            squadSlot = edge.squadSlotIndex;
            ++edges;
        }
    }
    if (edges != 1) {
        return false;
    }
    std::array<std::uint32_t, kPerformanceStateCapacity> seen{};
    std::size_t distinct = 0;
    std::size_t declaringMembers = 0;
    for (const format::Squad& squad : sdk::scenario_squads(catalog, *sdk::bound_scenario(*view))) {
        if (squad.slotIndex != squadSlot) {
            continue;
        }
        for (const format::SquadMember& member : sdk::squad_members(catalog, squad)) {
            const auto names = sdk::actor_class_state_names(catalog, member.actorClassIndex);
            if (names.empty()) {
                continue;
            }
            ++declaringMembers;
            bool holds = false;
            for (const format::ActorStateName& name : names) {
                holds = holds || name.nameHash == nameHash;
                const auto end = seen.begin() + static_cast<std::ptrdiff_t>(distinct);
                if (std::find(seen.begin(), end, name.nameHash) != end) {
                    continue;
                }
                if (distinct == seen.size()) {
                    return false;
                }
                seen[distinct++] = name.nameHash;
            }
            if (nameHash != 0 && !holds) {
                return false;
            }
        }
    }
    if (declaringMembers == 0 || (nameHash == 0 && distinct != 1)) {
        return false;
    }
    output.slotRow = slotRow;
    output.nameHash = nameHash != 0 ? nameHash : seen[0];
    output.stateCount = static_cast<std::uint32_t>(distinct);
    return true;
}

/** Compares every value-owned field captured for one generation-bound Slot handle. */
[[nodiscard]] bool same_slot_definition(const lua_vm::SlotDefinition& left,
                                        const lua_vm::SlotDefinition& right) noexcept {
    return left.id == right.id && left.name == right.name && left.objectId == right.objectId
           && left.senseSchemaId == right.senseSchemaId && left.authSchemaId == right.authSchemaId
           && left.nativeRow == right.nativeRow && left.localRow == right.localRow
           && left.objectTag == right.objectTag && left.registryKey == right.registryKey
           && left.slotIndex == right.slotIndex && left.slotType == right.slotType
           && left.componentClass == right.componentClass && left.senseSchema == right.senseSchema
           && left.authSchema == right.authSchema && left.flags == right.flags;
}

[[nodiscard]] bool scene_slot(const sdk::Catalog& catalog, const format::Slot& slot) noexcept {
    return slot.slotType == format::kAuthoredSceneSlotType
           && slot.componentClass == format::kAuthoredSceneComponentClass
           && slot.senseSchema == format::kAuthoredSceneSenseSchema
           && slot.authSchema == format::kAuthoredSceneAuthSchema
           && (slot.flags & format::kSlotSchemaJoinExact) != 0
           && sdk::slot_authored_scene_resources(catalog, slot).size() == 1;
}

/** Refuses scene identities that cannot fit the fixed Lua definition buffer. */
[[nodiscard]] bool scene_id(const sdk::Catalog& catalog,
                            const format::Occurrence& occurrence,
                            const format::Slot& slot,
                            lua_vm::SceneDefinition& output) noexcept {
    const std::string_view occurrenceId = catalog.string(occurrence.id);
    const std::string_view suffix = occurrenceId.starts_with(kOccurrencePrefix)
                                        ? occurrenceId.substr(kOccurrencePrefix.size())
                                        : occurrenceId;
    if (suffix.empty() || suffix.size() > 400
        || slot.slotIndex > (std::numeric_limits<std::uint16_t>::max)()
        || slot.slotType > (std::numeric_limits<std::uint16_t>::max)()) {
        return false;
    }
    const int written = std::snprintf(output.id.data(),
                                      output.id.size(),
                                      "symbol/%.*s/%04x/%04x",
                                      static_cast<int>(suffix.size()),
                                      suffix.data(),
                                      slot.slotIndex,
                                      slot.slotType);
    if (written <= 0 || static_cast<std::size_t>(written) >= output.id.size()) {
        output.id = {};
        return false;
    }
    output.idLength = static_cast<std::size_t>(written);
    return true;
}

/**
 * Resolves one authored scene of the bound activity into its Lua-facing definition.
 * Requires one matching exact scene slot across all scenario occurrences.
 */
template <typename Select>
[[nodiscard]] bool resolve_scene(const sdk::BoundView& view,
                                 Select&& select,
                                 lua_vm::SceneDefinition& output) noexcept {
    output = {};
    const sdk::Catalog& catalog = *view.catalog;
    const format::Scenario* const scenario = sdk::bound_scenario(view);
    const auto allOccurrences = catalog.occurrences();
    const auto allSlots = catalog.slots();
    const auto occurrences = sdk::scenario_occurrences(catalog, *scenario);
    std::uint32_t localRow = 0;
    std::size_t matches = 0;
    lua_vm::SceneDefinition selected{};
    for (const format::Occurrence& occurrence : occurrences) {
        if (occurrence.objectIndex >= catalog.objects().size()
            || &occurrence < allOccurrences.data()
            || &occurrence >= allOccurrences.data() + allOccurrences.size()) {
            return false;
        }
        const format::Object& object = catalog.objects()[occurrence.objectIndex];
        for (const format::Slot& slot : sdk::object_slots(catalog, object)) {
            if (!scene_slot(catalog, slot)) {
                continue;
            }
            ++localRow;
            lua_vm::SceneDefinition candidate{};
            candidate.localRow = localRow;
            candidate.occurrenceRow =
                static_cast<std::uint32_t>(&occurrence - allOccurrences.data());
            candidate.slotRow = static_cast<std::uint32_t>(&slot - allSlots.data());
            if (!scene_id(catalog, occurrence, slot, candidate)) {
                return false;
            }
            if (!select(candidate)) {
                continue;
            }
            selected = candidate;
            ++matches;
        }
    }
    if (matches != 1) {
        return false;
    }
    output = selected;
    return true;
}

/** Treats Lua scene rows as one-based positions inside the bound scenario. */
[[nodiscard]] bool resolve_scene_row(const void* context,
                                     std::uint32_t localRow,
                                     lua_vm::SceneDefinition& output) noexcept {
    const sdk::BoundView* const view = context_view(context);
    return valid_view(view) && localRow != 0
           && resolve_scene(
               *view,
               [localRow](const lua_vm::SceneDefinition& value) noexcept {
                   return value.localRow == localRow;
               },
               output);
}

/** Accepts the generated scene symbol or the generated ID/name of its owning slot. */
[[nodiscard]] bool resolve_scene_id(const void* context,
                                    std::string_view id,
                                    lua_vm::SceneDefinition& output) noexcept {
    const sdk::BoundView* const view = context_view(context);
    return valid_view(view) && !id.empty()
           && resolve_scene(
               *view,
               [view, id](const lua_vm::SceneDefinition& value) noexcept {
                   if (std::string_view(value.id.data(), value.idLength) == id
                       || value.slotRow >= view->catalog->slots().size()) {
                       return std::string_view(value.id.data(), value.idLength) == id;
                   }
                   const format::Slot& slot = view->catalog->slots()[value.slotRow];
                   if (view->catalog->string(slot.id) == id
                       || view->catalog->string(slot.name) == id) {
                       return scenes::authored_scene_availability(
                                  *view, value.occurrenceRow, value.slotRow)
                              == scenes::SceneStatus::ready;
                   }
                   for (const format::Text& alias : sdk::slot_aliases(*view->catalog, slot)) {
                       if (view->catalog->string(alias.value) == id) {
                           return scenes::authored_scene_availability(
                                      *view, value.occurrenceRow, value.slotRow)
                                  == scenes::SceneStatus::ready;
                       }
                   }
                   return false;
               },
               output);
}

[[nodiscard]] std::size_t squad_count(const void* context) noexcept {
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view)) {
        return 0;
    }
    return sdk::scenario_squads(*view->catalog, *sdk::bound_scenario(*view)).size();
}

/** Returns zero when any scenario occurrence cannot be resolved safely. */
[[nodiscard]] std::size_t scene_count(const void* context) noexcept {
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view)) {
        return 0;
    }
    std::size_t count = 0;
    const sdk::Catalog& catalog = *view->catalog;
    for (const format::Occurrence& occurrence :
         sdk::scenario_occurrences(catalog, *sdk::bound_scenario(*view))) {
        if (occurrence.objectIndex >= catalog.objects().size()) {
            return 0;
        }
        for (const format::Slot& slot :
             sdk::object_slots(catalog, catalog.objects()[occurrence.objectIndex])) {
            if (scene_slot(catalog, slot)) {
                ++count;
            }
        }
    }
    return count;
}

/** Counts repeated object definitions once and rejects an invalid occurrence set. */
[[nodiscard]] std::size_t slot_count(const void* context) noexcept {
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view)) {
        return 0;
    }
    const sdk::Catalog& catalog = *view->catalog;
    const auto occurrences = sdk::scenario_occurrences(catalog, *sdk::bound_scenario(*view));
    const auto objects = catalog.objects();
    std::size_t count = 0;
    for (std::size_t occurrenceIndex = 0; occurrenceIndex < occurrences.size(); ++occurrenceIndex) {
        const format::Occurrence& occurrence = occurrences[occurrenceIndex];
        if (occurrence.objectIndex >= objects.size()) {
            return 0;
        }
        if (!object_seen_before(occurrences, occurrenceIndex)) {
            count += sdk::object_slots(catalog, objects[occurrence.objectIndex]).size();
        }
    }
    return count;
}

/** @return Sensor count of the bound task, or 0 when nothing is bound. */
[[nodiscard]] std::size_t task_sensor_count(const void* context) noexcept {
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view)) {
        return 0;
    }
    return static_cast<std::size_t>(std::count_if(view->catalog->task_targets().begin(),
                                                  view->catalog->task_targets().end(),
                                                  [view](const format::TaskTarget& row) {
                                                      return scenario_has_slot(*view,
                                                                               row.taskSlotIndex);
                                                  }));
}

/** @return Element count of the bound directive, or 0 when nothing is bound. */
[[nodiscard]] std::size_t directive_element_count(const void* context) noexcept {
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view)) {
        return 0;
    }
    return static_cast<std::size_t>(std::count_if(view->catalog->directive_elements().begin(),
                                                  view->catalog->directive_elements().end(),
                                                  [view](const format::DirectiveElement& row) {
                                                      return scenario_has_slot(*view,
                                                                               row.slotIndex);
                                                  }));
}

[[nodiscard]] char hex_digit(std::uint8_t value) noexcept {
    // Lowercase hex digits; every digest and byte field is spelled this way.
    constexpr char digits[] = "0123456789abcdef";
    return digits[value & 0xFU];
}

} // namespace

/** Copies the bound activity's binding definition out of the catalog. */
bool activity_binding_definition(const sdk::Catalog& catalog,
                                 const format::Activity& activity,
                                 lua_vm::ActivityBindingDefinition& output) noexcept {
    output = {};
    const auto activities = catalog.activities();
    if (activities.empty()) {
        return false;
    }
    const auto first = reinterpret_cast<std::uintptr_t>(activities.data());
    const auto selected = reinterpret_cast<std::uintptr_t>(&activity);
    const std::size_t bytes = activities.size_bytes();
    if (selected < first || selected - first >= bytes
        || (selected - first) % sizeof(format::Activity) != 0) {
        return false;
    }
    output.internalName = catalog.string(activity.internalName);
    output.displayName = catalog.string(activity.displayName);
    output.selectedActivityRootTag = activity.selectedActivityRootTag;
    output.selectedScenarioTag = activity.selectedScenarioTag;
    output.matchmakingConfigTag = activity.matchmakingConfigTag;
    output.joinStatus = activity.joinStatus;
    output.bindingDisposition = activity.bindingDisposition;
    output.bindingReason = activity.bindingReason;
    output.bindingEvidenceBasis = activity.bindingEvidenceBasis;
    output.runnableStatus = activity.runnableStatus;
    output.fullSdkAcceptable =
        (activity.bindingFlags & format::kActivityBindingFullSdkAcceptable) != 0;
    output.hasInternalName = (activity.bindingFlags & format::kActivityBindingHasInternalName) != 0;
    output.hasMatchmakingConfig =
        (activity.bindingFlags & format::kActivityBindingHasMatchmakingConfig) != 0;
    return true;
}

/** @return The binding tag span one kind names, or empty when the kind is unknown. */
std::span<const format::ActivityBindingTag>
activity_binding_tags(const sdk::Catalog& catalog,
                      const format::Activity& activity,
                      lua_vm::ActivityBindingTagKind kind) noexcept {
    switch (kind) {
    case lua_vm::ActivityBindingTagKind::activityRootCandidates:
        return sdk::activity_root_candidate_tags(catalog, activity);
    case lua_vm::ActivityBindingTagKind::scenarioNameCandidates:
        return sdk::activity_scenario_name_candidate_tags(catalog, activity);
    case lua_vm::ActivityBindingTagKind::evidenceRoots:
        return sdk::activity_evidence_root_tags(catalog, activity);
    }
    return {};
}

/** Publishes only bound activity identities with a complete 32-byte SDK digest. */
bool program_identity(const sdk::BoundView& view,
                      bool publicTarget,
                      lua_vm::ProgramIdentity& output) noexcept {
    output = {};
    const format::Activity* const activity = sdk::bound_activity(view);
    if (activity == nullptr || view.catalog == nullptr) {
        return false;
    }
    const auto digest = view.catalog->sdk_build_sha256();
    const std::string_view activityId = view.catalog->string(activity->id);
    if (digest.size() != 32 || activityId.empty() || activityId.size() >= output.activityId.size()
        || view.activityRow == (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    // The generated SDK build identity is spelled as this prefix and 64 hex digits.
    constexpr std::string_view prefix = "sha256:";
    std::copy(prefix.begin(), prefix.end(), output.sdkBuildId.begin());
    std::size_t cursor = prefix.size();
    for (const std::byte byte : digest) {
        const auto value = static_cast<std::uint8_t>(byte);
        output.sdkBuildId[cursor++] = hex_digit(static_cast<std::uint8_t>(value >> 4U));
        output.sdkBuildId[cursor++] = hex_digit(value);
    }
    std::copy(activityId.begin(), activityId.end(), output.activityId.begin());
    output.activityRow = view.activityRow + 1;
    output.definitionHash = activity->definitionHash;
    output.publicTarget = publicTarget;
    return true;
}

/** The caller must keep the immutable view alive while Lua uses the returned callbacks. */
lua_vm::DefinitionApi definition_api(const sdk::BoundView& view) noexcept {
    lua_vm::DefinitionApi output{
        .context = &view,
        .resolveSquadRow = &resolve_squad_row,
        .resolveSquadId = &resolve_squad_id,
        .resolveSceneRow = &resolve_scene_row,
        .resolveSceneId = &resolve_scene_id,
        .resolveSlotRow = &resolve_slot_row,
        .resolveSlotId = &resolve_slot_id,
        .resolveSenseSlot = &resolve_sense_slot,
        .resolveTaskSensorRow = &resolve_task_sensor_row,
        .resolveTaskSensorId = &resolve_task_sensor_id,
        .resolveDirectiveElementRow = &resolve_directive_element_row,
        .resolveDirectiveElement = &resolve_directive_element,
        .resolvePerformanceState = &resolve_performance_state,
        .resolveActivityBinding = &resolve_activity_binding,
        .activityBindingTagCount = &activity_binding_tag_count,
        .resolveActivityBindingTag = &resolve_activity_binding_tag,
        .activityBindingLocatorCount = &activity_binding_locator_count,
        .resolveActivityBindingLocator = &resolve_activity_binding_locator,
        .squadCount = &squad_count,
        .sceneCount = &scene_count,
        .slotCount = &slot_count,
        .taskSensorCount = &task_sensor_count,
        .directiveElementCount = &directive_element_count,
    };
    message_catalog::attach(output);
    output.catalog = catalog_definition_api(view);
    return output;
}

} // namespace sunrise::server::activity::mission::sdk_bridge
