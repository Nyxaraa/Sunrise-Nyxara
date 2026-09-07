#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sunrise::server::activity::mission::lua_vm {

/** Every value that owns one generated-world Lua handle generation. */
struct WorldGenerationIdentity final {
    std::array<std::byte, 32> sdkBuildSha256{};
    std::array<std::byte, 32> sdkPayloadSha256{};
    std::array<std::byte, 32> sourceFingerprint{};
    std::array<std::byte, 32> manifestPayloadSha256{};
    std::array<std::byte, 32> shardPayloadSha256{};
    std::uint64_t activityClientGeneration{};
    std::uint32_t activityRow{};
    std::uint32_t scenarioTag{};

    [[nodiscard]] bool operator==(const WorldGenerationIdentity&) const noexcept = default;
};

/** Read-only collections exposed by one exact scenario shard. */
enum class WorldCollectionKind : std::uint8_t {
    squadAnchors,
    bubbles,
    states,
    objects,
    slots,
    descriptors,
    embeddedPlacementLinks,
    authoredPlacements,
    embeddedPlacements,
    typedReferences,
    containerPlacementLists,
    containerPlacementOwners,
    containerPlacements,
    containerPlacementConfigs,
    containerPlacementComponents,
    type23PlacementLinks,
    type23PlacementCandidates,
    staticSpatialTables,
    staticSpatialOwners,
    staticSpatialInstances,
    triggerVolumeTables,
    triggerVolumeOwners,
    triggerVolumeIncomingReferences,
    triggerVolumes,
    triggerVolumeVertices,
    triggerVolumeTriangles,
    names,
    tagNames,
    nameCandidates,
    inlineNameCandidates,
    authoredSquadConfigContexts,
    authoredSquadPlacementContexts,
    authoredSquadPointContexts,
    authoredSquadPointPlacementMatches,
    authoredSquadEdgeContexts,
};

/** Closed value shapes returned by one generated-world row field read. */
enum class WorldFieldKind : std::uint8_t {
    absent,
    unsignedInteger,
    signedInteger,
    unsignedDecimalString,
    signedDecimalString,
    real,
    boolean,
    string,
    optionalString,
    optionalRow,
    vector,
    bytes,
};

/** One copied or immediately consumed immutable field value. */
struct WorldFieldDefinition final {
    WorldFieldKind kind{WorldFieldKind::absent};
    std::uint64_t unsignedValue{};
    std::int64_t signedValue{};
    double realValue{};
    std::string_view stringValue{};
    std::array<float, 4> vectorValue{};
    std::array<std::byte, 32> bytesValue{};
    std::uint8_t valueCount{};
};

/** One selected package name plus the evidence retained for that selection. */
struct WorldNameDefinition final {
    std::string_view value{};
    std::uint32_t candidateCount{};
    std::uint32_t sourceTag{};
    std::uint32_t sourceClassId{};
    std::uint32_t provenance{};
    bool strongestTierOverflow{};
};

/** One exact main-pack squad anchor in the bound scenario. */
struct SquadAnchorDefinition final {
    std::string_view id{};
    std::array<float, 3> position{};
    std::uint64_t placedEntryIdentity{};
    std::uint32_t row{};
    std::uint32_t collectionRow{};
    std::uint32_t squadRow{};
    std::uint32_t pointOrdinal{};
    std::uint32_t objectListTag{};
    std::uint32_t placementOrdinal{};
    std::uint32_t flags{};
};

/** One exact object-state placement from the generated scenario shard. */
struct AuthoredPlacementDefinition final {
    WorldNameDefinition objectListName{};
    WorldNameDefinition classListName{};
    std::array<float, 4> rotation{};
    std::array<float, 3> position{};
    std::array<std::uint32_t, 4> rotationBits{};
    std::array<std::uint32_t, 3> positionBits{};
    std::uint64_t sourceOffset{};
    std::uint64_t identifier{};
    std::int64_t auxiliaryRelative{};
    float uniformScale{};
    std::uint32_t row{};
    std::uint32_t sourceObjectRow{};
    std::uint32_t bubbleRow{};
    std::uint32_t stateRow{};
    std::int32_t declaredBubbleIndex{};
    std::uint32_t objectListTag{};
    std::uint32_t classListTag{};
    std::uint32_t entryIndex{};
    std::uint32_t objectListNameRow{};
    std::uint32_t classListNameRow{};
    std::uint32_t uniformScaleBits{};
    std::uint32_t nameHash{};
    std::uint32_t placementFlagsRaw{};
    std::uint32_t context{};
};

/** One exact descriptor-embedded placement from the generated scenario shard. */
struct EmbeddedPlacementDefinition final {
    WorldNameDefinition classListName{};
    std::array<float, 4> rotation{};
    std::array<float, 3> position{};
    std::uint64_t sourceOffset{};
    std::uint64_t identifier{};
    std::int64_t auxiliaryRelative{};
    std::uint64_t auxiliaryOffset{};
    std::uint32_t row{};
    std::uint32_t linkRow{};
    std::uint32_t entryIndex{};
    std::uint32_t classListTag{};
    std::uint32_t classListNameRow{};
    std::uint32_t nameHash{};
    float fourthLane{};
    std::uint8_t replicationByte{};
    std::uint8_t gameworldByte{};
    std::uint8_t objectType{};
    bool hasAuxiliary{};
    bool objectTypeRead{};
};

/** One exact container placement and its parent list identity. */
struct ContainerPlacementDefinition final {
    WorldNameDefinition objectListName{};
    WorldNameDefinition resourceName{};
    WorldNameDefinition classListName{};
    std::array<float, 4> rotation{};
    std::array<float, 3> position{};
    std::uint64_t placementIdentifier{};
    std::uint32_t row{};
    std::uint32_t listRow{};
    std::uint32_t objectListTag{};
    std::uint32_t resourceTag{};
    std::uint32_t resourceClass{};
    std::uint32_t entryIndex{};
    std::uint32_t classListTag{};
    std::uint32_t classListNameRow{};
    std::uint32_t firstConfigRow{};
    std::uint32_t configCount{};
    float uniformScale{};
    std::uint8_t objectType{};
    bool resourceFieldRead{};
    bool resourceResolved{};
    bool listComplete{};
    bool placementIdentifierRead{};
    bool complete{};
};

/** One exact static spatial row and its table-level names. */
struct StaticSpatialDefinition final {
    WorldNameDefinition tableName{};
    WorldNameDefinition boundsName{};
    WorldNameDefinition resourceName{};
    std::array<float, 4> rotation{};
    std::array<float, 4> position{};
    std::array<float, 4> scale{};
    std::array<float, 4> localMinimum{};
    std::array<float, 4> localMaximum{};
    std::array<std::byte, 16> boundsOpaque{};
    std::uint32_t row{};
    std::uint32_t tableRow{};
    std::uint32_t instanceIndex{};
    std::uint32_t tableTag{};
    std::uint32_t boundsTag{};
    std::uint32_t resourceTag{};
    std::uint32_t resourceNameRow{};
    bool tableComplete{};
};

/** One exact trigger-volume row and its complete geometry ranges. */
struct TriggerVolumeDefinition final {
    WorldNameDefinition configName{};
    WorldNameDefinition classDefinitionName{};
    WorldNameDefinition shapeResourceName{};
    std::array<float, 4> rotation{};
    std::array<float, 4> position{};
    std::array<float, 4> minimum{};
    std::array<float, 4> maximum{};
    std::uint32_t row{};
    std::uint32_t tableRow{};
    std::uint32_t authoredRowIndex{};
    std::uint32_t configTag{};
    std::uint32_t identityMatchCount{};
    std::uint32_t registryKey{};
    std::uint32_t componentOrdinal{};
    std::uint32_t slotIndex{};
    std::uint32_t slotType{};
    std::uint32_t classDefinitionTag{};
    std::uint32_t classDefinitionNameRow{};
    std::uint32_t shapeResourceTag{};
    std::uint32_t shapeResourceNameRow{};
    std::uint32_t shapeReferenceWord{};
    std::uint32_t shapeIndex{};
    std::uint32_t firstVertexRow{};
    std::uint32_t vertexCount{};
    std::uint32_t firstTriangleRow{};
    std::uint32_t triangleCount{};
    std::uint32_t flags{};
    float extrusion{};
    std::uint8_t active{};
    bool tableComplete{};
    bool complete{};
};

/** One row result from the closed positioned-family selector. */
struct WorldRowDefinition final {
    WorldCollectionKind kind{WorldCollectionKind::squadAnchors};
    SquadAnchorDefinition squadAnchor{};
    AuthoredPlacementDefinition authoredPlacement{};
    EmbeddedPlacementDefinition embeddedPlacement{};
    ContainerPlacementDefinition containerPlacement{};
    StaticSpatialDefinition staticSpatial{};
    TriggerVolumeDefinition triggerVolume{};
};

/** One world-coordinate trigger vertex. */
struct TriggerVertexDefinition final {
    std::array<float, 4> value{};
    std::uint32_t row{};
    std::uint32_t localRow{};
};

/** One trigger triangle whose indices address its owning volume's vertex range. */
struct TriggerTriangleDefinition final {
    std::array<std::uint8_t, 3> indices{};
    std::uint32_t row{};
    std::uint32_t localRow{};
};

/** Exact extraction diagnostics retained by all exposed world families. */
struct WorldDiagnosticsDefinition final {
    std::string_view detail{};
    std::uint64_t revision{};
    std::uint64_t request{};
    std::uint64_t unresolvedReads{};
    std::uint64_t containerUnresolvedReads{};
    std::uint64_t containerSemanticUnresolved{};
    std::uint64_t containerDroppedLists{};
    std::uint64_t containerDroppedOwners{};
    std::uint64_t containerDroppedPlacements{};
    std::uint64_t containerDroppedConfigs{};
    std::uint64_t containerDroppedComponents{};
    std::uint64_t embeddedApplicableDescriptors{};
    std::uint64_t embeddedEmptyDescriptors{};
    std::uint64_t embeddedReadPlacements{};
    std::uint64_t embeddedUnreadConfigurations{};
    std::uint64_t embeddedMalformedDescriptors{};
    std::uint64_t embeddedMalformedPlacements{};
    std::uint64_t embeddedUnresolvedClassDefinitions{};
    std::uint64_t embeddedDroppedLinks{};
    std::uint64_t embeddedDroppedPlacements{};
    std::uint64_t type23UnreadIdentifiers{};
    std::uint64_t type23DroppedLinks{};
    std::uint64_t type23DroppedCandidates{};
    std::uint64_t type23ZeroIdentityMatches{};
    std::uint64_t type23MultipleIdentityMatches{};
    std::uint64_t type23ZeroActiveCandidates{};
    std::uint64_t type23MultipleActiveCandidates{};
    std::uint64_t staticSpatialUnresolvedReads{};
    std::uint64_t staticSpatialSemanticUnresolved{};
    std::uint64_t staticSpatialDropped{};
    std::uint64_t triggerUnresolvedReads{};
    std::uint64_t triggerDroppedTables{};
    std::uint64_t triggerDroppedOwners{};
    std::uint64_t triggerDroppedInstances{};
    std::uint64_t triggerDroppedVertices{};
    std::uint64_t triggerDroppedTriangles{};
    std::uint64_t triggerDroppedIncomingReferences{};
    std::uint64_t triggerZeroMatches{};
    std::uint64_t triggerMultipleMatches{};
    std::uint32_t status{};
    std::uint32_t coverage{};
    bool containerContextResolved{};
    bool containerContextNotApplicable{};
    bool containerIdentityOwnerInventoryComplete{};
    bool containerComplete{};
    bool embeddedComplete{};
    bool type23Complete{};
    bool staticSpatialContextResolved{};
    bool staticSpatialNotApplicable{};
    bool staticSpatialComplete{};
    bool triggerComplete{};
};

/** One exact structural-family coverage row retained by the scenario shard. */
struct WorldCoverageDefinition final {
    std::string_view family{};
    std::uint32_t row{};
    std::uint32_t familyIndex{};
    std::uint32_t status{};
    std::uint32_t lossMask{};
};

using WorldDefinitionCount = std::size_t (*)(const void* context,
                                             const WorldGenerationIdentity& generation,
                                             WorldCollectionKind kind) noexcept;
using ValidateWorldGeneration = bool (*)(const void* context,
                                         const WorldGenerationIdentity& generation) noexcept;
using ResolveWorldDefinition = bool (*)(const void* context,
                                        const WorldGenerationIdentity& generation,
                                        WorldCollectionKind kind,
                                        std::uint32_t localRow,
                                        WorldRowDefinition& output) noexcept;
using ResolveWorldFieldDefinition = bool (*)(const void* context,
                                             const WorldGenerationIdentity& generation,
                                             WorldCollectionKind kind,
                                             std::uint32_t localRow,
                                             std::string_view key,
                                             WorldFieldDefinition& output) noexcept;
using SquadAnchorDefinitionCount = std::size_t (*)(const void* context,
                                                   const WorldGenerationIdentity& generation,
                                                   std::uint32_t squadRow) noexcept;
using ResolveSquadAnchorDefinition = bool (*)(const void* context,
                                              const WorldGenerationIdentity& generation,
                                              std::uint32_t squadRow,
                                              std::uint32_t localRow,
                                              SquadAnchorDefinition& output) noexcept;
using ResolveTriggerVertexDefinition = bool (*)(const void* context,
                                                const WorldGenerationIdentity& generation,
                                                std::uint32_t triggerRow,
                                                std::uint32_t localRow,
                                                TriggerVertexDefinition& output) noexcept;
using ResolveTriggerTriangleDefinition = bool (*)(const void* context,
                                                  const WorldGenerationIdentity& generation,
                                                  std::uint32_t triggerRow,
                                                  std::uint32_t localRow,
                                                  TriggerTriangleDefinition& output) noexcept;
using ResolveWorldDiagnostics = bool (*)(const void* context,
                                         const WorldGenerationIdentity& generation,
                                         WorldDiagnosticsDefinition& output) noexcept;
using WorldCoverageDefinitionCount =
    std::size_t (*)(const void* context, const WorldGenerationIdentity& generation) noexcept;
using ResolveWorldCoverageDefinition = bool (*)(const void* context,
                                                const WorldGenerationIdentity& generation,
                                                std::uint32_t localRow,
                                                WorldCoverageDefinition& output) noexcept;

/** Immutable generated-world callbacks used only by locked Lua views. */
struct WorldDefinitionApi final {
    const void* context{};
    std::string_view scenarioName{};
    WorldGenerationIdentity generation{};
    ValidateWorldGeneration validate{};
    WorldDefinitionCount count{};
    ResolveWorldDefinition resolve{};
    ResolveWorldFieldDefinition resolveField{};
    SquadAnchorDefinitionCount squadAnchorCount{};
    ResolveSquadAnchorDefinition resolveSquadAnchor{};
    ResolveTriggerVertexDefinition resolveTriggerVertex{};
    ResolveTriggerTriangleDefinition resolveTriggerTriangle{};
    ResolveWorldDiagnostics resolveDiagnostics{};
    WorldCoverageDefinitionCount coverageCount{};
    ResolveWorldCoverageDefinition resolveCoverage{};
};

} // namespace sunrise::server::activity::mission::lua_vm
