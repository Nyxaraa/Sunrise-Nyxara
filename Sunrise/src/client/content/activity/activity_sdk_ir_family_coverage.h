#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sunrise::client::content::activity::sdk_generation::ir_family_coverage {

/** Canonical native order of every normalized SDK family. Keep this in lockstep with IR v5. */
enum class Family : std::uint8_t {
    sources,
    buildProfile,
    packages,
    activities,
    contentActivities,
    namedRoots,
    scenarios,
    bubbles,
    states,
    entries,
    registries,
    registryObjectEdges,
    objects,
    objectOccurrences,
    slots,
    missionSeeds,
    missionAuthGroups,
    missionIntentMappings,
    placedSubblocks,
    placedLeaves,
    placedHops,
    placedBareTargets,
    placedConfigOccurrences,
    configs,
    configContexts,
    authoredSpawners,
    authoredSpawnerMembers,
    authoredSpawnerCandidates,
    actorClasses,
    rsatDescriptors,
    rsatSchemas,
    authoredSpawnRules,
    authoredSpawnPoints,
    authoredPlacements,
    authoredPlacementContexts,
    spawnPointPlacementMatches,
    authoredSpawnerRuleEdges,
    descriptors,
    authoredSceneResources,
    authoredSceneSquadEdges,
    typedReferenceDefs,
    typedReferences,
    symbolTemplates,
    symbolDeclarations,
    slotFamilies,
    schemas,
    activityMessages,
    behaviorPrograms,
    behaviorEdges,
    behaviorPaths,
    behaviorUnresolved,
    hostApi,
    capabilities,
    refusals,
    validation,
    count,
};

// A coverage report carries one row per family.
inline constexpr std::size_t kFamilyCount = static_cast<std::size_t>(Family::count);
static_assert(kFamilyCount == 55);

/** Families are grouped only to make coverage reports and missing-producer failures actionable. */
enum class Domain : std::uint8_t {
    sourceIdentity,
    activityGraph,
    worldGraph,
    squadGraph,
    schemaGraph,
    behaviorGraph,
    apiPolicy,
    validation,
};

/** Exactness rule used to prove that a family was not merely mentioned by a producer. */
enum class Projection : std::uint8_t {
    exactRows,
    losslessDerivedRows,
    singletonMetadata,
    diagnosticRows,
};

/** A completed producer either retained rows or proved an empty installed-content census. */
enum class Status : std::uint8_t {
    unresolved,
    exact,
    verifiedEmpty,
};

/** Stable identity and ownership of one required SDK family. */
struct Descriptor final {
    Family family{};
    std::string_view name{};
    Domain domain{};
};

/** Final aggregate supplied by the producer that owns one family. */
struct Declaration final {
    std::uint64_t sourceRecordCount{};
    std::uint64_t outputRowCount{};
    std::uint64_t discardedSourceRecordCount{};
    std::uint32_t contributorCount{};
    Projection projection{Projection::exactRows};
    Status status{Status::unresolved};
};

/** Stable reason returned by the fail-closed SDK-completeness gate. */
enum class Failure : std::uint8_t {
    none,
    invalidFamily,
    duplicateDeclaration,
    missingDeclaration,
    unresolvedFamily,
    missingContributor,
    discardedSourceRows,
    exactRowCountMismatch,
    invalidExactEmpty,
    invalidVerifiedEmpty,
    invalidSingleton,
    aggregateOverflow,
};

/** Complete aggregate emitted only after all 55 declarations close. */
struct Summary final {
    std::uint64_t sourceRecordCount{};
    std::uint64_t outputRowCount{};
    std::uint32_t exactFamilyCount{};
    std::uint32_t verifiedEmptyFamilyCount{};

    [[nodiscard]] bool operator==(const Summary&) const noexcept = default;
};

/** Fixed-size ledger; no producer can silently invent, omit, or redeclare a family. */
struct Ledger final {
    std::array<Declaration, kFamilyCount> declarations{};
    std::array<bool, kFamilyCount> declared{};
    Failure fault{Failure::none};
    Family faultFamily{Family::count};
};

/** @return The canonical ordered 55-family contract. */
[[nodiscard]] const std::array<Descriptor, kFamilyCount>& descriptors() noexcept;

/** @return Exact snake-case family spelling, or an empty view for an invalid enum. */
[[nodiscard]] std::string_view family_name(Family family) noexcept;

/** Resolves only an exact canonical family spelling. */
[[nodiscard]] bool family_from_name(std::string_view name, Family& output) noexcept;

/** Adds one final aggregate. A duplicate or invalid declaration permanently faults the ledger. */
[[nodiscard]] bool declare(Ledger& ledger, Family family, const Declaration& declaration) noexcept;

/** Requires an exact or verified-empty declaration for every canonical family. */
[[nodiscard]] bool
close(const Ledger& ledger, Summary& output, Failure& failure, Family& failureFamily) noexcept;

/** @return Stable lowercase spelling for logs and generated contracts. */
[[nodiscard]] std::string_view stable_name(Failure failure) noexcept;

/** @return Stable lowercase spelling used by the generated coverage artifact. */
[[nodiscard]] std::string_view stable_name(Domain domain) noexcept;
[[nodiscard]] std::string_view stable_name(Projection projection) noexcept;
[[nodiscard]] std::string_view stable_name(Status status) noexcept;

/** Renders the complete ledger as deterministic JSON; incomplete ledgers produce no bytes. */
[[nodiscard]] bool render_json(const Ledger& ledger,
                               std::string& output,
                               Failure& failure,
                               Family& failureFamily) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::ir_family_coverage
