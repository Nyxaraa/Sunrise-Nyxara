#include "activity_sdk_ir_family_coverage.h"

#include <limits>

namespace sunrise::client::content::activity::sdk_generation::ir_family_coverage {
namespace {

using enum Family;

// Short domain aliases keep the descriptor rows below the column limit.
constexpr Domain kSourceIdentity = Domain::sourceIdentity;
constexpr Domain kActivityGraph = Domain::activityGraph;
constexpr Domain kWorldGraph = Domain::worldGraph;
constexpr Domain kSquadGraph = Domain::squadGraph;
constexpr Domain kSchemaGraph = Domain::schemaGraph;
constexpr Domain kBehaviorGraph = Domain::behaviorGraph;
constexpr Domain kApiPolicy = Domain::apiPolicy;
constexpr Domain kValidation = Domain::validation;

// One row per family: the family, its stable report name, and its domain.
constexpr std::array<Descriptor, kFamilyCount> kDescriptors{{
    {sources, "sources", kSourceIdentity},
    {buildProfile, "build_profile", kSourceIdentity},
    {packages, "packages", kSourceIdentity},
    {activities, "activities", kActivityGraph},
    {contentActivities, "content_activities", kActivityGraph},
    {namedRoots, "named_roots", kActivityGraph},
    {scenarios, "scenarios", kActivityGraph},
    {bubbles, "bubbles", kWorldGraph},
    {states, "states", kWorldGraph},
    {entries, "entries", kWorldGraph},
    {registries, "registries", kWorldGraph},
    {registryObjectEdges, "registry_object_edges", kWorldGraph},
    {objects, "objects", kWorldGraph},
    {objectOccurrences, "object_occurrences", kWorldGraph},
    {slots, "slots", kWorldGraph},
    {missionSeeds, "mission_seeds", kActivityGraph},
    {missionAuthGroups, "mission_auth_groups", kActivityGraph},
    {missionIntentMappings, "mission_intent_mappings", kActivityGraph},
    {placedSubblocks, "placed_subblocks", kWorldGraph},
    {placedLeaves, "placed_leaves", kWorldGraph},
    {placedHops, "placed_hops", kWorldGraph},
    {placedBareTargets, "placed_bare_targets", kWorldGraph},
    {placedConfigOccurrences, "placed_config_occurrences", kWorldGraph},
    {configs, "configs", kWorldGraph},
    {configContexts, "config_contexts", kWorldGraph},
    {authoredSpawners, "authored_spawners", kSquadGraph},
    {authoredSpawnerMembers, "authored_spawner_members", kSquadGraph},
    {authoredSpawnerCandidates, "authored_spawner_candidates", kSquadGraph},
    {actorClasses, "actor_classes", kSquadGraph},
    {rsatDescriptors, "rsat_descriptors", kSquadGraph},
    {rsatSchemas, "rsat_schemas", kSquadGraph},
    {authoredSpawnRules, "authored_spawn_rules", kSquadGraph},
    {authoredSpawnPoints, "authored_spawn_points", kSquadGraph},
    {authoredPlacements, "authored_placements", kWorldGraph},
    {authoredPlacementContexts, "authored_placement_contexts", kWorldGraph},
    {spawnPointPlacementMatches, "spawn_point_placement_matches", kSquadGraph},
    {authoredSpawnerRuleEdges, "authored_spawner_rule_edges", kSquadGraph},
    {descriptors, "descriptors", kSchemaGraph},
    {authoredSceneResources, "authored_scene_resources", kWorldGraph},
    {authoredSceneSquadEdges, "authored_scene_squad_edges", kSquadGraph},
    {typedReferenceDefs, "typed_reference_defs", kSchemaGraph},
    {typedReferences, "typed_references", kSchemaGraph},
    {symbolTemplates, "symbol_templates", kSchemaGraph},
    {symbolDeclarations, "symbol_declarations", kSchemaGraph},
    {slotFamilies, "slot_families", kSchemaGraph},
    {schemas, "schemas", kSchemaGraph},
    {activityMessages, "activity_messages", kSchemaGraph},
    {behaviorPrograms, "behavior_programs", kBehaviorGraph},
    {behaviorEdges, "behavior_edges", kBehaviorGraph},
    {behaviorPaths, "behavior_paths", kBehaviorGraph},
    {behaviorUnresolved, "behavior_unresolved", kBehaviorGraph},
    {hostApi, "host_api", kApiPolicy},
    {capabilities, "capabilities", kApiPolicy},
    {refusals, "refusals", kApiPolicy},
    {validation, "validation", kValidation},
}};

static_assert([] {
    for (std::size_t index = 0; index < kDescriptors.size(); ++index) {
        if (static_cast<std::size_t>(kDescriptors[index].family) != index
            || kDescriptors[index].name.empty()) {
            return false;
        }
    }
    return true;
}());

/** Adds one aggregate counter without allowing a wrapped completeness report. */
[[nodiscard]] bool checked_add(std::uint64_t value, std::uint64_t& aggregate) noexcept {
    if (value > (std::numeric_limits<std::uint64_t>::max)() - aggregate) {
        return false;
    }
    aggregate += value;
    return true;
}

} // namespace

const std::array<Descriptor, kFamilyCount>& descriptors() noexcept {
    return kDescriptors;
}

std::string_view family_name(Family family) noexcept {
    const std::size_t index = static_cast<std::size_t>(family);
    return index < kDescriptors.size() ? kDescriptors[index].name : std::string_view{};
}

bool family_from_name(std::string_view name, Family& output) noexcept {
    output = Family::count;
    for (const Descriptor& descriptor : kDescriptors) {
        if (descriptor.name == name) {
            output = descriptor.family;
            return true;
        }
    }
    return false;
}

/** Records one family's declaration in the ledger. @return False when it conflicts. */
bool declare(Ledger& ledger, Family family, const Declaration& declaration) noexcept {
    const std::size_t index = static_cast<std::size_t>(family);
    if (ledger.fault != Failure::none) {
        return false;
    }
    if (index >= ledger.declarations.size()) {
        ledger.fault = Failure::invalidFamily;
        ledger.faultFamily = Family::count;
        return false;
    }
    if (ledger.declared[index]) {
        ledger.fault = Failure::duplicateDeclaration;
        ledger.faultFamily = family;
        return false;
    }
    ledger.declarations[index] = declaration;
    ledger.declared[index] = true;
    return true;
}

/** Closes the ledger, checking every declared family reached its declared coverage. */
bool close(const Ledger& ledger,
           Summary& output,
           Failure& failure,
           Family& failureFamily) noexcept {
    output = {};
    failure = ledger.fault;
    failureFamily = ledger.faultFamily;
    if (failure != Failure::none) {
        return false;
    }
    for (std::size_t index = 0; index < ledger.declarations.size(); ++index) {
        const Family family = static_cast<Family>(index);
        if (!ledger.declared[index]) {
            failure = Failure::missingDeclaration;
            failureFamily = family;
            return false;
        }
        const Declaration& row = ledger.declarations[index];
        if (row.status == Status::unresolved) {
            failure = Failure::unresolvedFamily;
            failureFamily = family;
            return false;
        }
        if (row.contributorCount == 0) {
            failure = Failure::missingContributor;
            failureFamily = family;
            return false;
        }
        if (row.discardedSourceRecordCount != 0) {
            failure = Failure::discardedSourceRows;
            failureFamily = family;
            return false;
        }
        if (row.projection == Projection::exactRows
            && row.sourceRecordCount != row.outputRowCount) {
            failure = Failure::exactRowCountMismatch;
            failureFamily = family;
            return false;
        }
        if (row.status == Status::exact && row.sourceRecordCount == 0 && row.outputRowCount == 0) {
            failure = Failure::invalidExactEmpty;
            failureFamily = family;
            return false;
        }
        if (row.status == Status::verifiedEmpty
            && (row.sourceRecordCount != 0 || row.outputRowCount != 0)) {
            failure = Failure::invalidVerifiedEmpty;
            failureFamily = family;
            return false;
        }
        if (row.projection == Projection::singletonMetadata && row.outputRowCount != 1) {
            failure = Failure::invalidSingleton;
            failureFamily = family;
            return false;
        }
        if (!checked_add(row.sourceRecordCount, output.sourceRecordCount)
            || !checked_add(row.outputRowCount, output.outputRowCount)) {
            output = {};
            failure = Failure::aggregateOverflow;
            failureFamily = family;
            return false;
        }
        if (row.status == Status::exact) {
            ++output.exactFamilyCount;
        } else {
            ++output.verifiedEmptyFamilyCount;
        }
    }
    failure = Failure::none;
    failureFamily = Family::count;
    return true;
}

/** @return The stable name of one coverage failure. */
std::string_view stable_name(Failure failure) noexcept {
    switch (failure) {
    case Failure::none:
        return "none";
    case Failure::invalidFamily:
        return "invalid_family";
    case Failure::duplicateDeclaration:
        return "duplicate_declaration";
    case Failure::missingDeclaration:
        return "missing_declaration";
    case Failure::unresolvedFamily:
        return "unresolved_family";
    case Failure::missingContributor:
        return "missing_contributor";
    case Failure::discardedSourceRows:
        return "discarded_source_rows";
    case Failure::exactRowCountMismatch:
        return "exact_row_count_mismatch";
    case Failure::invalidExactEmpty:
        return "invalid_exact_empty";
    case Failure::invalidVerifiedEmpty:
        return "invalid_verified_empty";
    case Failure::invalidSingleton:
        return "invalid_singleton";
    case Failure::aggregateOverflow:
        return "aggregate_overflow";
    }
    return {};
}

/** @return The stable name of one coverage domain. */
std::string_view stable_name(Domain domain) noexcept {
    switch (domain) {
    case Domain::sourceIdentity:
        return "source_identity";
    case Domain::activityGraph:
        return "activity_graph";
    case Domain::worldGraph:
        return "world_graph";
    case Domain::squadGraph:
        return "squad_graph";
    case Domain::schemaGraph:
        return "schema_graph";
    case Domain::behaviorGraph:
        return "behavior_graph";
    case Domain::apiPolicy:
        return "api_policy";
    case Domain::validation:
        return "validation";
    }
    return {};
}

/** @return The stable name of one coverage projection. */
std::string_view stable_name(Projection projection) noexcept {
    switch (projection) {
    case Projection::exactRows:
        return "exact_rows";
    case Projection::losslessDerivedRows:
        return "lossless_derived_rows";
    case Projection::singletonMetadata:
        return "singleton_metadata";
    case Projection::diagnosticRows:
        return "diagnostic_rows";
    }
    return {};
}

/** @return The stable report name of one coverage status. */
std::string_view stable_name(Status status) noexcept {
    switch (status) {
    case Status::unresolved:
        return "unresolved";
    case Status::exact:
        return "exact";
    case Status::verifiedEmpty:
        return "verified_empty";
    }
    return {};
}

/** Renders the ledger as the JSON coverage report. @return False when the buffer is short. */
bool render_json(const Ledger& ledger,
                 std::string& output,
                 Failure& failure,
                 Family& failureFamily) noexcept {
    output.clear();
    Summary summary{};
    if (!close(ledger, summary, failure, failureFamily)) {
        return false;
    }
    try {
        output.append("{\n  \"complete\": true,\n  \"families\": [\n");
        for (std::size_t index = 0; index < kDescriptors.size(); ++index) {
            const Descriptor& descriptor = kDescriptors[index];
            const Declaration& declaration = ledger.declarations[index];
            output.append("    {\n      \"contributors\": ");
            output.append(std::to_string(declaration.contributorCount));
            output.append(",\n      \"discarded_source_records\": \"");
            output.append(std::to_string(declaration.discardedSourceRecordCount));
            output.append("\",\n      \"domain\": \"");
            output.append(stable_name(descriptor.domain));
            output.append("\",\n      \"family_index\": ");
            output.append(std::to_string(index));
            output.append(",\n      \"name\": \"");
            output.append(descriptor.name);
            output.append("\",\n      \"output_rows\": \"");
            output.append(std::to_string(declaration.outputRowCount));
            output.append("\",\n      \"projection\": \"");
            output.append(stable_name(declaration.projection));
            output.append("\",\n      \"source_records\": \"");
            output.append(std::to_string(declaration.sourceRecordCount));
            output.append("\",\n      \"status\": \"");
            output.append(stable_name(declaration.status));
            output.append(index + 1 == kDescriptors.size() ? "\"\n    }\n" : "\"\n    },\n");
        }
        output.append("  ],\n  \"family_count\": 55,\n  \"schema\": "
                      "\"sunrise-activity-sdk-ir-family-coverage-v1\",\n  \"summary\": {\n"
                      "    \"exact_families\": ");
        output.append(std::to_string(summary.exactFamilyCount));
        output.append(",\n    \"output_rows\": \"");
        output.append(std::to_string(summary.outputRowCount));
        output.append("\",\n    \"source_records\": \"");
        output.append(std::to_string(summary.sourceRecordCount));
        output.append("\",\n    \"verified_empty_families\": ");
        output.append(std::to_string(summary.verifiedEmptyFamilyCount));
        output.append("\n  }\n}\n");
        return true;
    } catch (...) {
        output.clear();
        failure = Failure::aggregateOverflow;
        failureFamily = Family::count;
        return false;
    }
}

} // namespace sunrise::client::content::activity::sdk_generation::ir_family_coverage
