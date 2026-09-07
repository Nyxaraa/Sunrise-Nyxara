#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "definition_index_table.h"

namespace sunrise::middleware::content::packages::tables {

/** Config fields holding the two self-relative component pointers. */
inline constexpr std::size_t kConfigPrimaryComponentPointerOffset = 0x10;
inline constexpr std::size_t kConfigSecondaryComponentPointerOffset = 0x18;

/** Exact component classes and slot type of an authored type-1 squad spawner. */
inline constexpr std::uint32_t kAuthoredSquadSpawnerPrimaryClass = 0x80809A3BU;
inline constexpr std::uint32_t kAuthoredSquadSpawnerSecondaryClass = 0x8080948FU;
inline constexpr std::uint16_t kAuthoredSquadSpawnerSlotType = 1;
/** Spawner-secondary fields for its two raw object-slot refs and member array. */
inline constexpr std::size_t kAuthoredSquadSpawnerRawReference98Offset = 0x98;
inline constexpr std::size_t kAuthoredSquadSpawnerRawReferenceA0Offset = 0xA0;
inline constexpr std::size_t kAuthoredSquadSpawnerMemberArrayOffset = 0xA8;
/** Exact member array element class and stride. */
inline constexpr std::uint32_t kAuthoredSquadMemberClass = 0x80808356U;
inline constexpr std::size_t kAuthoredSquadMemberStride = 0x68;
/** Six consecutive candidate-array descriptors begin after the member key and reserved word. */
inline constexpr std::size_t kAuthoredSquadVariantCount = 6;
inline constexpr std::size_t kAuthoredSquadFirstCandidateArrayOffset = 8;
inline constexpr std::size_t kAuthoredSquadCandidateArrayStride = 16;
/** Exact candidate array element class and stride. */
inline constexpr std::uint32_t kAuthoredSquadCandidateClass = 0x80808358U;
inline constexpr std::size_t kAuthoredSquadCandidateStride = 0x18;

/** Exact component classes and slot type of an authored type-66 spawn rule. */
inline constexpr std::uint32_t kAuthoredSquadRulePrimaryClass = 0x808094CFU;
inline constexpr std::uint32_t kAuthoredSquadRuleSecondaryClass = 0x808094D0U;
inline constexpr std::uint16_t kAuthoredSquadRuleSlotType = 66;
/** Rule-primary field and exact type of its authored point array. */
inline constexpr std::size_t kAuthoredSquadRulePointArrayOffset = 0x1A0;
inline constexpr std::uint32_t kAuthoredSquadRulePointClass = 0x80809840U;
inline constexpr std::size_t kAuthoredSquadRulePointStride = 0x48;

/**
 * A spawner with both raw refs absent may carry its own point set. The primary component points
 * at a point-set component, the secondary at its placement peer. The pair is reciprocal.
 */
inline constexpr std::uint32_t kAuthoredSquadInlinePointSetClass = 0x80807EBEU;
inline constexpr std::uint32_t kAuthoredSquadInlinePlacementClass = 0x80807EBFU;
inline constexpr std::size_t kAuthoredSquadSpawnerInlinePointSetOffset = 0x5D0;
inline constexpr std::size_t kAuthoredSquadSpawnerInlinePlacementOffset = 0xB8;
/** Point-set fields: the typed point array and the initial point index. */
inline constexpr std::size_t kAuthoredSquadInlinePointArrayOffset = 0x40;
inline constexpr std::size_t kAuthoredSquadInlineInitialPointOffset = 0x80;
/** The placement record sits inside the peer component and has no class marker before it. */
inline constexpr std::size_t kAuthoredSquadInlinePlacementRecordOffset = 0x10;

/** Exact placed-entry marker and layout reached by one non-null candidate pointer. */
inline constexpr std::uint32_t kAuthoredSquadPlacementClass = 0x808099D8U;
inline constexpr std::size_t kAuthoredSquadPlacementStride = 0x90;
inline constexpr std::size_t kAuthoredSquadPlacementRotationOffset = 0x10;
inline constexpr std::size_t kAuthoredSquadPlacementPositionOffset = 0x20;
inline constexpr std::size_t kAuthoredSquadPlacementScaleOffset = 0x2C;
inline constexpr std::size_t kAuthoredSquadPlacementNameHashOffset = 0x64;
inline constexpr std::size_t kAuthoredSquadPlacementFlagsOffset = 0x68;
inline constexpr std::size_t kAuthoredSquadPlacementIdentityOffset = 0x70;
inline constexpr std::uint64_t kAbsentAuthoredSquadPlacementIdentity = 0xFFFFFFFFFFFFFFFFULL;

/** The two config components after their owner, class, peer class, and peer offset agree. */
struct ComponentPair final {
    std::size_t primaryOffset{};
    std::size_t secondaryOffset{};
    std::uint32_t primaryClass{};
    std::uint32_t secondaryClass{};
};

/** Exact package tables owned by one authored type-1 spawner config. */
struct AuthoredSquadSpawner final {
    ComponentPair components{};
    std::uint64_t rawReference98{};
    std::uint64_t rawReferenceA0{};
    Array members{};
};

/** One fixed-stride member and its six exact typed candidate arrays. */
struct AuthoredSquadMember final {
    std::uint32_t key{};
    std::uint32_t reserved{};
    std::array<Array, kAuthoredSquadVariantCount> candidates{};
};

/** One candidate whose placed-entry pointer and complete finite transform validated. */
struct AuthoredSquadCandidate final {
    std::size_t descriptorOffset{};
    std::int64_t placementRelative{};
    std::size_t placementOffset{};
    std::array<std::byte, 16> descriptorTail{};
    bool hasPlacement{};
    std::uint32_t placementClass{};
    std::uint32_t classDefinitionTag{};
    std::array<float, 4> rotation{};
    std::array<std::uint32_t, 4> rotationBits{};
    std::array<float, 3> position{};
    std::array<std::uint32_t, 3> positionBits{};
    float uniformScale{};
    std::uint32_t uniformScaleBits{};
    std::uint32_t nameHash{};
    std::uint32_t placementFlagsRaw{};
    std::uint64_t placementIdentity{};
};

/** One spawner's own point set and the placement its single point names. */
struct AuthoredSquadInlinePointSet final {
    std::size_t pointSetOffset{};
    std::size_t placementComponentOffset{};
    std::size_t placementOffset{};
    Array points{};
    std::uint32_t initialPointIndex{};
    /** The placement fields. Descriptor and pointer fields stay zero. */
    AuthoredSquadCandidate placement{};
};

/** Exact package table owned by one authored type-66 spawn-rule config. */
struct AuthoredSquadRule final {
    ComponentPair components{};
    Array points{};
};

/** One fixed-stride authored rule point. Its remaining bytes stay opaque. */
struct AuthoredSquadRulePoint final {
    std::size_t rowOffset{};
    std::uint64_t placementIdentity{};
    std::array<std::byte, 64> rawTail{};
};

/** Reads the config's two reciprocal component endpoints without classifying the pair. */
[[nodiscard]] bool config_component_pair(std::span<const std::byte> blob,
                                         std::uint32_t configTag,
                                         ComponentPair& output) noexcept;

/** Reads the exact type-1 spawner pair, raw references, and bounded member array. */
[[nodiscard]] bool authored_squad_spawner(std::span<const std::byte> blob,
                                          std::uint32_t configTag,
                                          AuthoredSquadSpawner& output) noexcept;

/** Reads one member and validates all six typed candidate-array descriptors. */
[[nodiscard]] bool authored_squad_member_at(std::span<const std::byte> blob,
                                            const AuthoredSquadSpawner& spawner,
                                            std::uint64_t index,
                                            AuthoredSquadMember& output) noexcept;

/**
 * Reads one usable candidate. Null targets, wrong placement markers, non-finite transforms, and
 * zero/all-one placement identities are refused.
 */
[[nodiscard]] bool authored_squad_candidate_at(std::span<const std::byte> blob,
                                               const AuthoredSquadMember& member,
                                               std::size_t variant,
                                               std::uint64_t index,
                                               AuthoredSquadCandidate& output) noexcept;

/**
 * Reads the complete SDK candidate record. Unlike the runnable-candidate helper above, this
 * preserves null targets and zero/all-one authored identities exactly.
 */
[[nodiscard]] bool authored_squad_candidate_record_at(std::span<const std::byte> blob,
                                                      const AuthoredSquadMember& member,
                                                      std::size_t variant,
                                                      std::uint64_t index,
                                                      AuthoredSquadCandidate& output) noexcept;

/**
 * Reads a spawner's own point set. `present` is false when the spawner has none. A present but
 * malformed pair fails.
 */
[[nodiscard]] bool authored_squad_inline_point_set(std::span<const std::byte> blob,
                                                   std::uint32_t configTag,
                                                   const AuthoredSquadSpawner& spawner,
                                                   bool& present,
                                                   AuthoredSquadInlinePointSet& output) noexcept;

/** Reads the exact type-66 rule pair and bounded typed point array. */
[[nodiscard]] bool authored_squad_rule(std::span<const std::byte> blob,
                                       std::uint32_t configTag,
                                       AuthoredSquadRule& output) noexcept;

/** Reads one identity from a validated authored rule point. */
[[nodiscard]] bool authored_squad_rule_point_at(std::span<const std::byte> blob,
                                                const AuthoredSquadRule& rule,
                                                std::uint64_t index,
                                                AuthoredSquadRulePoint& output) noexcept;

} // namespace sunrise::middleware::content::packages::tables
