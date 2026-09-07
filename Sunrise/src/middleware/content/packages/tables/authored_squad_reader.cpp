#include "authored_squad_reader.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

#include "internal.h"

namespace sunrise::middleware::content::packages::tables {
namespace {

/** Exact Tiger typed-array marker and the builder's fail-closed count ceiling. */
constexpr std::uint32_t kArrayMarker = 0x80809FBDU;
constexpr std::int64_t kMaximumArrayCount = 1'000'000;
/** Bytes from the marker through the typed array header. */
constexpr std::size_t kArrayMarkerAndHeaderSize = 20;
constexpr std::size_t kArrayHeaderMarkerBack = 4;
constexpr std::size_t kArrayHeaderDataOffset = 16;

/** One component endpoint before the reciprocal pair check. */
struct ComponentEndpoint final {
    std::size_t offset{};
    std::uint32_t componentClass{};
    std::uint32_t peerClass{};
    std::uint64_t peerOffset{};
};

/** @return True when one complete range lies in the package blob. */
[[nodiscard]] bool
contains(std::span<const std::byte> blob, std::size_t offset, std::size_t size) noexcept {
    return offset <= blob.size() && size <= blob.size() - offset;
}

/** Adds one unsigned field offset without wrapping host size arithmetic. */
[[nodiscard]] bool add_offset(std::size_t base, std::size_t delta, std::size_t& output) noexcept {
    if (base > (std::numeric_limits<std::size_t>::max)() - delta) {
        return false;
    }
    output = base + delta;
    return true;
}

/** Adds one signed self-relative field without wrapping host size arithmetic. */
[[nodiscard]] bool
relative_offset(std::size_t base, std::int64_t relative, std::size_t& output) noexcept {
    /** Signed range the addition must stay inside. */
    constexpr std::int64_t kMaximum = (std::numeric_limits<std::int64_t>::max)();
    constexpr std::int64_t kMinimum = (std::numeric_limits<std::int64_t>::min)();
    if (base > static_cast<std::size_t>(kMaximum)) {
        return false;
    }
    const auto signedBase = static_cast<std::int64_t>(base);
    if ((relative > 0 && signedBase > kMaximum - relative)
        || (relative < 0 && signedBase < kMinimum - relative)) {
        return false;
    }
    const std::int64_t target = signedBase + relative;
    if (target < 0) {
        return false;
    }
    output = static_cast<std::size_t>(target);
    return true;
}

/**
 * Resolves the exact `{i64 count, i64 self-relative header}` typed-array shape. Empty arrays may
 * use either the canonical null pointer or a complete typed header.
 */
[[nodiscard]] bool typed_array(std::span<const std::byte> blob,
                               std::size_t field,
                               std::uint32_t expectedClass,
                               std::size_t stride,
                               Array& output) noexcept {
    output = {};
    std::int64_t count = 0;
    std::int64_t relative = 0;
    std::size_t relativeField = 0;
    if (!add_offset(field, sizeof count, relativeField) || !read(blob, field, count)
        || !read(blob, relativeField, relative) || count < 0 || count > kMaximumArrayCount) {
        return false;
    }
    if (count == 0 && relative == 0) {
        return true;
    }

    std::size_t header = 0;
    if (!relative_offset(relativeField, relative, header) || header < kArrayHeaderMarkerBack
        || !contains(blob, header - kArrayHeaderMarkerBack, kArrayMarkerAndHeaderSize)) {
        return false;
    }

    std::uint32_t marker = 0;
    std::int64_t repeatedCount = 0;
    std::uint32_t elementClass = 0;
    std::uint32_t padding = 0;
    if (!read(blob, header - kArrayHeaderMarkerBack, marker) || marker != kArrayMarker
        || !read(blob, header, repeatedCount) || repeatedCount != count
        || !read(blob, header + 8, elementClass) || elementClass != expectedClass
        || !read(blob, header + 12, padding) || padding != 0) {
        return false;
    }

    const std::size_t data = header + kArrayHeaderDataOffset;
    const auto unsignedCount = static_cast<std::uint64_t>(count);
    if (data > blob.size()
        || (unsignedCount != 0
            && unsignedCount > static_cast<std::uint64_t>((blob.size() - data) / stride))) {
        return false;
    }
    output = Array{unsignedCount, data, elementClass};
    return true;
}

/** Finds one validated row and rechecks the exact element class and whole-array bound. */
[[nodiscard]] bool array_row(std::span<const std::byte> blob,
                             const Array& array,
                             std::uint32_t expectedClass,
                             std::size_t stride,
                             std::uint64_t index,
                             std::size_t& output) noexcept {
    output = 0;
    if (array.elementClass != expectedClass || index >= array.count
        || array.dataOffset > blob.size()
        || array.count > static_cast<std::uint64_t>((blob.size() - array.dataOffset) / stride)) {
        return false;
    }
    output = array.dataOffset + static_cast<std::size_t>(index) * stride;
    return true;
}

/** Reads one config pointer and its owner/class/peer tuple. */
[[nodiscard]] bool component_endpoint(std::span<const std::byte> blob,
                                      std::uint32_t configTag,
                                      std::size_t field,
                                      ComponentEndpoint& output) noexcept {
    output = {};
    std::int64_t relative = 0;
    std::size_t target = 0;
    if (!read(blob, field, relative) || !relative_offset(field, relative, target)
        || target < sizeof(std::uint32_t)
        || !contains(blob, target - sizeof(std::uint32_t), kArrayMarkerAndHeaderSize)) {
        return false;
    }
    std::uint32_t owner = 0;
    ComponentEndpoint candidate{};
    candidate.offset = target;
    if (!read(blob, target - 4, candidate.componentClass) || !read(blob, target, owner)
        || owner != configTag || !read(blob, target + 4, candidate.peerClass)
        || !read(blob, target + 8, candidate.peerOffset)) {
        return false;
    }
    output = candidate;
    return true;
}

/** Reads one fixed float vector and rejects every non-finite lane. */
template <std::size_t Size>
[[nodiscard]] bool read_finite_vector(std::span<const std::byte> blob,
                                      std::size_t offset,
                                      std::array<float, Size>& output) noexcept {
    output = {};
    for (std::size_t lane = 0; lane < Size; ++lane) {
        if (!read(blob, offset + lane * sizeof(float), output[lane])
            || !std::isfinite(output[lane])) {
            output = {};
            return false;
        }
    }
    return true;
}

/** Reads the placement fields at one record offset. The caller has checked the class marker. */
[[nodiscard]] bool read_placement_fields(std::span<const std::byte> blob,
                                         std::size_t placementOffset,
                                         AuthoredSquadCandidate& candidate) noexcept {
    if (!contains(blob, placementOffset, kAuthoredSquadPlacementStride)
        || !read(blob, placementOffset, candidate.classDefinitionTag)
        || !read_finite_vector(
            blob, placementOffset + kAuthoredSquadPlacementRotationOffset, candidate.rotation)
        || !read_finite_vector(
            blob, placementOffset + kAuthoredSquadPlacementPositionOffset, candidate.position)
        || !read(blob, placementOffset + kAuthoredSquadPlacementScaleOffset, candidate.uniformScale)
        || !std::isfinite(candidate.uniformScale)
        || !read(blob, placementOffset + kAuthoredSquadPlacementNameHashOffset, candidate.nameHash)
        || !read(
            blob, placementOffset + kAuthoredSquadPlacementFlagsOffset, candidate.placementFlagsRaw)
        || !read(blob,
                 placementOffset + kAuthoredSquadPlacementIdentityOffset,
                 candidate.placementIdentity)) {
        return false;
    }
    for (std::size_t lane = 0; lane < candidate.rotation.size(); ++lane) {
        candidate.rotationBits[lane] = std::bit_cast<std::uint32_t>(candidate.rotation[lane]);
    }
    for (std::size_t lane = 0; lane < candidate.position.size(); ++lane) {
        candidate.positionBits[lane] = std::bit_cast<std::uint32_t>(candidate.position[lane]);
    }
    candidate.uniformScaleBits = std::bit_cast<std::uint32_t>(candidate.uniformScale);
    candidate.placementOffset = placementOffset;
    candidate.hasPlacement = true;
    return true;
}

} // namespace

/** Reads the exact reciprocal component-pair contract shared by both config kinds. */
bool config_component_pair(std::span<const std::byte> blob,
                           std::uint32_t configTag,
                           ComponentPair& output) noexcept {
    output = {};
    ComponentEndpoint primary{};
    ComponentEndpoint secondary{};
    if (!component_endpoint(blob, configTag, kConfigPrimaryComponentPointerOffset, primary)
        || !component_endpoint(blob, configTag, kConfigSecondaryComponentPointerOffset, secondary)
        || primary.peerClass != secondary.componentClass
        || secondary.peerClass != primary.componentClass
        || primary.peerOffset != static_cast<std::uint64_t>(secondary.offset)
        || secondary.peerOffset != static_cast<std::uint64_t>(primary.offset)) {
        return false;
    }
    output = ComponentPair{
        primary.offset, secondary.offset, primary.componentClass, secondary.componentClass};
    return true;
}

/** Reads the exact type-1 spawner pair, raw references, and member array. */
bool authored_squad_spawner(std::span<const std::byte> blob,
                            std::uint32_t configTag,
                            AuthoredSquadSpawner& output) noexcept {
    output = {};
    AuthoredSquadSpawner candidate{};
    if (!config_component_pair(blob, configTag, candidate.components)
        || candidate.components.primaryClass != kAuthoredSquadSpawnerPrimaryClass
        || candidate.components.secondaryClass != kAuthoredSquadSpawnerSecondaryClass) {
        return false;
    }
    std::size_t reference98 = 0;
    std::size_t referenceA0 = 0;
    std::size_t memberArray = 0;
    if (!add_offset(candidate.components.secondaryOffset,
                    kAuthoredSquadSpawnerRawReference98Offset,
                    reference98)
        || !add_offset(candidate.components.secondaryOffset,
                       kAuthoredSquadSpawnerRawReferenceA0Offset,
                       referenceA0)
        || !add_offset(candidate.components.secondaryOffset,
                       kAuthoredSquadSpawnerMemberArrayOffset,
                       memberArray)
        || !read(blob, reference98, candidate.rawReference98)
        || !read(blob, referenceA0, candidate.rawReferenceA0)
        || !typed_array(blob,
                        memberArray,
                        kAuthoredSquadMemberClass,
                        kAuthoredSquadMemberStride,
                        candidate.members)) {
        return false;
    }
    output = candidate;
    return true;
}

/** Reads one member and all six exact typed candidate arrays. */
bool authored_squad_member_at(std::span<const std::byte> blob,
                              const AuthoredSquadSpawner& spawner,
                              std::uint64_t index,
                              AuthoredSquadMember& output) noexcept {
    output = {};
    std::size_t row = 0;
    if (!array_row(blob,
                   spawner.members,
                   kAuthoredSquadMemberClass,
                   kAuthoredSquadMemberStride,
                   index,
                   row)) {
        return false;
    }
    AuthoredSquadMember candidate{};
    if (!read(blob, row, candidate.key) || !read(blob, row + 4, candidate.reserved)) {
        return false;
    }
    for (std::size_t variant = 0; variant < candidate.candidates.size(); ++variant) {
        const std::size_t field = row + kAuthoredSquadFirstCandidateArrayOffset
                                  + variant * kAuthoredSquadCandidateArrayStride;
        if (!typed_array(blob,
                         field,
                         kAuthoredSquadCandidateClass,
                         kAuthoredSquadCandidateStride,
                         candidate.candidates[variant])) {
            return false;
        }
    }
    output = candidate;
    return true;
}

/** Reads one complete candidate record while preserving null and sentinel-valued identities. */
bool authored_squad_candidate_record_at(std::span<const std::byte> blob,
                                        const AuthoredSquadMember& member,
                                        std::size_t variant,
                                        std::uint64_t index,
                                        AuthoredSquadCandidate& output) noexcept {
    output = {};
    if (variant >= member.candidates.size()) {
        return false;
    }
    AuthoredSquadCandidate candidate{};
    if (!array_row(blob,
                   member.candidates[variant],
                   kAuthoredSquadCandidateClass,
                   kAuthoredSquadCandidateStride,
                   index,
                   candidate.descriptorOffset)
        || !read(blob, candidate.descriptorOffset, candidate.placementRelative)
        || !contains(blob, candidate.descriptorOffset, kAuthoredSquadCandidateStride)) {
        return false;
    }
    std::memcpy(candidate.descriptorTail.data(),
                blob.data() + candidate.descriptorOffset + sizeof(candidate.placementRelative),
                candidate.descriptorTail.size());
    if (candidate.placementRelative == 0) {
        output = candidate;
        return true;
    }
    if (!relative_offset(
            candidate.descriptorOffset, candidate.placementRelative, candidate.placementOffset)
        || candidate.placementOffset < sizeof(std::uint32_t)
        || !contains(blob,
                     candidate.placementOffset - sizeof(std::uint32_t),
                     sizeof(std::uint32_t) + kAuthoredSquadPlacementStride)) {
        return false;
    }

    if (!read(blob, candidate.placementOffset - 4, candidate.placementClass)
        || candidate.placementClass != kAuthoredSquadPlacementClass
        || !read_placement_fields(blob, candidate.placementOffset, candidate)) {
        return false;
    }
    output = candidate;
    return true;
}

/** Reads a spawner's own point set and placement. Absent when the primary pointer is null. */
bool authored_squad_inline_point_set(std::span<const std::byte> blob,
                                     std::uint32_t configTag,
                                     const AuthoredSquadSpawner& spawner,
                                     bool& present,
                                     AuthoredSquadInlinePointSet& output) noexcept {
    output = {};
    present = false;
    std::size_t pointSetField = 0;
    std::int64_t pointSetRelative = 0;
    if (!add_offset(spawner.components.primaryOffset,
                    kAuthoredSquadSpawnerInlinePointSetOffset,
                    pointSetField)
        || !read(blob, pointSetField, pointSetRelative)) {
        return false;
    }
    if (pointSetRelative == 0) {
        return true;
    }
    std::size_t placementField = 0;
    ComponentEndpoint pointSet{};
    ComponentEndpoint placement{};
    if (!add_offset(spawner.components.secondaryOffset,
                    kAuthoredSquadSpawnerInlinePlacementOffset,
                    placementField)
        || !component_endpoint(blob, configTag, pointSetField, pointSet)
        || !component_endpoint(blob, configTag, placementField, placement)
        || pointSet.componentClass != kAuthoredSquadInlinePointSetClass
        || placement.componentClass != kAuthoredSquadInlinePlacementClass
        || pointSet.peerClass != placement.componentClass
        || placement.peerClass != pointSet.componentClass
        || pointSet.peerOffset != static_cast<std::uint64_t>(placement.offset)
        || placement.peerOffset != static_cast<std::uint64_t>(pointSet.offset)) {
        return false;
    }
    AuthoredSquadInlinePointSet candidate{};
    candidate.pointSetOffset = pointSet.offset;
    candidate.placementComponentOffset = placement.offset;
    std::size_t pointArray = 0;
    std::size_t initialPoint = 0;
    if (!add_offset(pointSet.offset, kAuthoredSquadInlinePointArrayOffset, pointArray)
        || !add_offset(pointSet.offset, kAuthoredSquadInlineInitialPointOffset, initialPoint)
        || !typed_array(blob,
                        pointArray,
                        kAuthoredSquadRulePointClass,
                        kAuthoredSquadRulePointStride,
                        candidate.points)
        || !read(blob, initialPoint, candidate.initialPointIndex)
        || !add_offset(
            placement.offset, kAuthoredSquadInlinePlacementRecordOffset, candidate.placementOffset)
        || !read_placement_fields(blob, candidate.placementOffset, candidate.placement)) {
        return false;
    }
    candidate.placement.placementClass = kAuthoredSquadPlacementClass;
    present = true;
    output = candidate;
    return true;
}

/** Reads one candidate only when its complete placed-entry target is usable. */
bool authored_squad_candidate_at(std::span<const std::byte> blob,
                                 const AuthoredSquadMember& member,
                                 std::size_t variant,
                                 std::uint64_t index,
                                 AuthoredSquadCandidate& output) noexcept {
    output = {};
    AuthoredSquadCandidate candidate{};
    if (!authored_squad_candidate_record_at(blob, member, variant, index, candidate)
        || !candidate.hasPlacement || candidate.placementIdentity == 0
        || candidate.placementIdentity == kAbsentAuthoredSquadPlacementIdentity) {
        return false;
    }
    output = candidate;
    return true;
}

/** Reads the exact type-66 rule pair and typed point array. */
bool authored_squad_rule(std::span<const std::byte> blob,
                         std::uint32_t configTag,
                         AuthoredSquadRule& output) noexcept {
    output = {};
    AuthoredSquadRule candidate{};
    if (!config_component_pair(blob, configTag, candidate.components)
        || candidate.components.primaryClass != kAuthoredSquadRulePrimaryClass
        || candidate.components.secondaryClass != kAuthoredSquadRuleSecondaryClass) {
        return false;
    }
    std::size_t pointArray = 0;
    if (!add_offset(
            candidate.components.primaryOffset, kAuthoredSquadRulePointArrayOffset, pointArray)
        || !typed_array(blob,
                        pointArray,
                        kAuthoredSquadRulePointClass,
                        kAuthoredSquadRulePointStride,
                        candidate.points)) {
        return false;
    }
    output = candidate;
    return true;
}

/** Reads one authored rule-point identity while leaving its tail opaque. */
bool authored_squad_rule_point_at(std::span<const std::byte> blob,
                                  const AuthoredSquadRule& rule,
                                  std::uint64_t index,
                                  AuthoredSquadRulePoint& output) noexcept {
    output = {};
    AuthoredSquadRulePoint candidate{};
    if (!array_row(blob,
                   rule.points,
                   kAuthoredSquadRulePointClass,
                   kAuthoredSquadRulePointStride,
                   index,
                   candidate.rowOffset)
        || !read(blob, candidate.rowOffset, candidate.placementIdentity)
        || !contains(blob, candidate.rowOffset, kAuthoredSquadRulePointStride)) {
        return false;
    }
    std::memcpy(candidate.rawTail.data(),
                blob.data() + candidate.rowOffset + sizeof(candidate.placementIdentity),
                candidate.rawTail.size());
    output = candidate;
    return true;
}

} // namespace sunrise::middleware::content::packages::tables
