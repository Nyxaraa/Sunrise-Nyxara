#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sunrise::server::activity::mission::lua_vm {

/** Every value that owns one immutable runtime-pack Lua handle generation. */
struct CatalogGenerationIdentity final {
    std::array<std::byte, 32> sdkBuildSha256{};
    std::array<std::byte, 32> sdkPayloadSha256{};
    std::array<std::byte, 32> contentKeySha256{};
    std::array<std::byte, 32> logicalIrSha256{};
    std::uint64_t activityClientGeneration{};
    std::uint32_t activityRow{};
    std::uint32_t scenarioRow{};
    std::uint32_t definitionHash{};
    std::uint32_t scenarioTag{};

    [[nodiscard]] bool operator==(const CatalogGenerationIdentity&) const noexcept = default;
};

/** Every runtime-pack row section that has a direct catalog collection. */
enum class CatalogCollectionKind : std::uint8_t {
    activities,
    scenarios,
    bubbles,
    states,
    objects,
    occurrences,
    slots,
    texts,
    capabilities,
    gates,
    refusals,
    actorClasses,
    rsatDescriptors,
    rsatSchemas,
    rsatFields,
    squads,
    squadMembers,
    squadAnchors,
    authoredSceneResources,
    authoredSceneSquadEdges,
    activityBindingLocators,
};

/** Activity-owned ranges that account for the non-direct binding-tag section. */
enum class CatalogActivityOwnedKind : std::uint8_t {
    activityRootCandidateTags,
    scenarioNameCandidateTags,
    evidenceRootTags,
    bindingLocators,
};

/** Closed lossless value shapes returned by one runtime-pack field read. */
enum class CatalogFieldKind : std::uint8_t {
    absent,
    unsignedInteger,
    signedInteger,
    unsignedDecimalString,
    signedDecimalString,
    string,
    bytes,
};

// Holds the widest raw row a catalog field copies, the 40-byte RSAT schema field row.
inline constexpr std::size_t kCatalogFieldByteCapacity = 40;

/** One copied or immediately consumed immutable runtime-pack field. */
struct CatalogFieldDefinition final {
    CatalogFieldKind kind{CatalogFieldKind::absent};
    std::uint64_t unsignedValue{};
    std::int64_t signedValue{};
    std::string_view stringValue{};
    std::array<std::byte, kCatalogFieldByteCapacity> bytesValue{};
    std::uint8_t valueCount{};
};

/** Raw zero-based owner range retained by one catalog activity row. */
struct CatalogOwnedRangeDefinition final {
    std::uint32_t firstIndex{};
    std::uint32_t count{};
};

using ValidateCatalogGeneration = bool (*)(const void* context,
                                           const CatalogGenerationIdentity& generation) noexcept;
using CatalogDefinitionCount = std::size_t (*)(const void* context,
                                               const CatalogGenerationIdentity& generation,
                                               CatalogCollectionKind kind) noexcept;
using ResolveCatalogFieldDefinition = bool (*)(const void* context,
                                               const CatalogGenerationIdentity& generation,
                                               CatalogCollectionKind kind,
                                               std::uint32_t localRow,
                                               std::string_view key,
                                               CatalogFieldDefinition& output) noexcept;
using ResolveCatalogActivityOwnedRange = bool (*)(const void* context,
                                                  const CatalogGenerationIdentity& generation,
                                                  std::uint32_t activityRow,
                                                  CatalogActivityOwnedKind kind,
                                                  CatalogOwnedRangeDefinition& output) noexcept;
using ResolveCatalogActivityOwnedTag = bool (*)(const void* context,
                                                const CatalogGenerationIdentity& generation,
                                                std::uint32_t activityRow,
                                                CatalogActivityOwnedKind kind,
                                                std::uint32_t localRow,
                                                std::uint32_t& output) noexcept;

/** Immutable complete runtime-pack callbacks used only by locked Lua views. */
struct CatalogDefinitionApi final {
    const void* context{};
    CatalogGenerationIdentity generation{};
    ValidateCatalogGeneration validate{};
    CatalogDefinitionCount count{};
    ResolveCatalogFieldDefinition resolveField{};
    ResolveCatalogActivityOwnedRange resolveActivityOwnedRange{};
    ResolveCatalogActivityOwnedTag resolveActivityOwnedTag{};
};

} // namespace sunrise::server::activity::mission::lua_vm
