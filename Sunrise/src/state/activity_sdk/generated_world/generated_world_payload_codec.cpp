#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../../middleware/crypto/sha256.h"
#include "../../build_data/scriptables/coverage.h"
#include "generated_world_scalar_codec.h"
#include "internal.h"

namespace sunrise::state::activity_sdk::generated_world::internal {

namespace catalog = build_data::scriptables;

namespace {

/** Exact native stride of every process-vector section, in format order. */
constexpr std::array<std::uint32_t, format::kSectionCount> kExpectedStrides{
    sizeof(catalog::Bubble),
    sizeof(catalog::State),
    sizeof(catalog::Object),
    sizeof(catalog::Slot),
    sizeof(catalog::Descriptor),
    sizeof(catalog::EmbeddedPlacementLink),
    sizeof(catalog::EmbeddedPlacement),
    sizeof(catalog::TypedReference),
    sizeof(catalog::AuthoredPlacement),
    sizeof(catalog::ContainerPlacementList),
    sizeof(catalog::ContainerPlacementOwner),
    sizeof(catalog::ContainerPlacement),
    sizeof(catalog::ContainerPlacementConfig),
    sizeof(catalog::ContainerPlacementComponent),
    sizeof(catalog::Type23PlacementLink),
    sizeof(catalog::Type23PlacementCandidate),
    sizeof(catalog::StaticSpatialTable),
    sizeof(catalog::StaticSpatialOwner),
    sizeof(catalog::StaticSpatialInstance),
    sizeof(catalog::TriggerVolumeTable),
    sizeof(catalog::TriggerVolumeOwner),
    sizeof(catalog::TriggerVolumeIncomingReference),
    sizeof(catalog::TriggerVolumeInstance),
    sizeof(catalog::TriggerVolumeVertex),
    sizeof(catalog::TriggerVolumeTriangle),
    sizeof(catalog::Name),
    sizeof(catalog::TagName),
    sizeof(catalog::NameCandidate),
    sizeof(catalog::InlineNameCandidate),
    sizeof(std::byte),
    sizeof(catalog::AuthoredSquadConfigContext),
    sizeof(catalog::AuthoredSquadPlacementContext),
    sizeof(catalog::AuthoredSquadPointContext),
    sizeof(catalog::AuthoredSquadPointPlacementMatch),
    sizeof(catalog::AuthoredSquadEdgeContext),
};

/** Read-only bytes of one fixed-layout native row before it becomes a typed object. */
template <typename Value> struct RawRow final {
    std::span<const std::byte, sizeof(Value)> bytes;
};

/** @return True when one raw native bool representation is zero or one. */
template <typename Value>
[[nodiscard]] bool valid_bool(RawRow<Value> row, std::size_t offset) noexcept {
    return offset < row.bytes.size() && std::to_integer<unsigned>(row.bytes[offset]) <= 1U;
}

/** @return True when one byte-backed enum does not exceed its last declared value. */
template <typename Value, typename Enum>
[[nodiscard]] bool valid_enum(RawRow<Value> row, std::size_t offset, Enum last) noexcept {
    static_assert(sizeof(Enum) == sizeof(std::uint8_t));
    return offset < row.bytes.size()
           && std::to_integer<unsigned>(row.bytes[offset])
                  <= static_cast<unsigned>(static_cast<std::uint8_t>(last));
}

/** Reads one trivially copied scalar from a raw row without forming an unaligned typed pointer. */
template <typename Scalar, typename Value>
[[nodiscard]] bool read_scalar(RawRow<Value> row, std::size_t offset, Scalar& output) noexcept {
    output = {};
    if (offset > row.bytes.size() || sizeof output > row.bytes.size() - offset) {
        return false;
    }
    std::memcpy(&output, row.bytes.data() + offset, sizeof output);
    return true;
}

[[nodiscard]] bool valid_row(RawRow<catalog::Bubble> row) noexcept {
    return valid_bool(row, offsetof(catalog::Bubble, isPublic));
}

[[nodiscard]] bool valid_row(RawRow<catalog::State> row) noexcept {
    return valid_bool(row, offsetof(catalog::State, enabled))
           && valid_bool(row, offsetof(catalog::State, resolved));
}

/** @return True when one raw object row's counts and inline ranges stay inside their bounds. */
[[nodiscard]] bool valid_row(RawRow<catalog::Object> row) noexcept {
    std::uint32_t configCount = 0;
    std::uint32_t subblockCount = 0;
    std::uint32_t leafCount = 0;
    std::uint32_t hopCount = 0;
    std::uint32_t bareTargetCount = 0;
    return valid_enum(row, offsetof(catalog::Object, safety), catalog::GroupSafety::ambiguous)
           && valid_bool(row, offsetof(catalog::Object, complete))
           && read_scalar(row, offsetof(catalog::Object, configCount), configCount)
           && read_scalar(row, offsetof(catalog::Object, placedSubblockCount), subblockCount)
           && read_scalar(row, offsetof(catalog::Object, placedLeafCount), leafCount)
           && read_scalar(row, offsetof(catalog::Object, placedHopCount), hopCount)
           && read_scalar(row, offsetof(catalog::Object, bareTargetCount), bareTargetCount)
           && (subblockCount != 0 || leafCount == 0)
           && (leafCount != 0 || (hopCount == 0 && configCount == 0 && bareTargetCount == 0))
           && configCount <= hopCount && bareTargetCount <= hopCount;
}

[[nodiscard]] bool valid_row(RawRow<catalog::Slot>) noexcept {
    return true;
}

[[nodiscard]] bool valid_row(RawRow<catalog::Descriptor> row) noexcept {
    return valid_bool(row, offsetof(catalog::Descriptor, placementIdentifierRead));
}

[[nodiscard]] bool valid_row(RawRow<catalog::EmbeddedPlacementLink> row) noexcept {
    return valid_bool(row, offsetof(catalog::EmbeddedPlacementLink, complete));
}

[[nodiscard]] bool valid_row(RawRow<catalog::EmbeddedPlacement> row) noexcept {
    return valid_bool(row, offsetof(catalog::EmbeddedPlacement, hasAuxiliary))
           && valid_bool(row, offsetof(catalog::EmbeddedPlacement, objectTypeRead));
}

[[nodiscard]] bool valid_row(RawRow<catalog::TypedReference> row) noexcept {
    return valid_enum(
        row, offsetof(catalog::TypedReference, join), catalog::ReferenceJoin::ambiguous);
}

[[nodiscard]] bool valid_row(RawRow<catalog::AuthoredPlacement> row) noexcept {
    return valid_enum(row,
                      offsetof(catalog::AuthoredPlacement, context),
                      catalog::SpatialContextJoin::packageStemBubble);
}

[[nodiscard]] bool valid_row(RawRow<catalog::ContainerPlacementList> row) noexcept {
    return valid_bool(row, offsetof(catalog::ContainerPlacementList, resourceFieldRead))
           && valid_bool(row, offsetof(catalog::ContainerPlacementList, resourceResolved))
           && valid_bool(row, offsetof(catalog::ContainerPlacementList, complete));
}

[[nodiscard]] bool valid_row(RawRow<catalog::ContainerPlacementOwner> row) noexcept {
    return valid_enum(row,
                      offsetof(catalog::ContainerPlacementOwner, context),
                      catalog::SpatialContextJoin::packageStemBubble);
}

[[nodiscard]] bool valid_row(RawRow<catalog::ContainerPlacement> row) noexcept {
    return valid_bool(row, offsetof(catalog::ContainerPlacement, placementIdentifierRead))
           && valid_bool(row, offsetof(catalog::ContainerPlacement, complete));
}

[[nodiscard]] bool valid_row(RawRow<catalog::ContainerPlacementConfig> row) noexcept {
    return valid_bool(row, offsetof(catalog::ContainerPlacementConfig, complete));
}

[[nodiscard]] bool valid_row(RawRow<catalog::ContainerPlacementComponent>) noexcept {
    return true;
}

[[nodiscard]] bool valid_row(RawRow<catalog::Type23PlacementLink> row) noexcept {
    return valid_enum(
               row, offsetof(catalog::Type23PlacementLink, join), catalog::ReferenceJoin::ambiguous)
           && valid_bool(row, offsetof(catalog::Type23PlacementLink, complete));
}

[[nodiscard]] bool valid_row(RawRow<catalog::Type23PlacementCandidate>) noexcept {
    return true;
}

[[nodiscard]] bool valid_row(RawRow<catalog::StaticSpatialTable> row) noexcept {
    return valid_bool(row, offsetof(catalog::StaticSpatialTable, complete));
}

[[nodiscard]] bool valid_row(RawRow<catalog::StaticSpatialOwner> row) noexcept {
    return valid_enum(row,
                      offsetof(catalog::StaticSpatialOwner, context),
                      catalog::SpatialContextJoin::packageStemBubble);
}

[[nodiscard]] bool valid_row(RawRow<catalog::StaticSpatialInstance>) noexcept {
    return true;
}

[[nodiscard]] bool valid_row(RawRow<catalog::TriggerVolumeTable> row) noexcept {
    return valid_bool(row, offsetof(catalog::TriggerVolumeTable, complete));
}

[[nodiscard]] bool valid_row(RawRow<catalog::TriggerVolumeOwner> row) noexcept {
    return valid_enum(
        row, offsetof(catalog::TriggerVolumeOwner, slotJoin), catalog::ReferenceJoin::ambiguous);
}

[[nodiscard]] bool valid_row(RawRow<catalog::TriggerVolumeIncomingReference>) noexcept {
    return true;
}

[[nodiscard]] bool valid_row(RawRow<catalog::TriggerVolumeInstance> row) noexcept {
    return valid_bool(row, offsetof(catalog::TriggerVolumeInstance, complete));
}

[[nodiscard]] bool valid_row(RawRow<catalog::TriggerVolumeVertex>) noexcept {
    return true;
}

[[nodiscard]] bool valid_row(RawRow<catalog::TriggerVolumeTriangle>) noexcept {
    return true;
}

[[nodiscard]] bool valid_row(RawRow<catalog::Name> row) noexcept {
    return valid_enum(row,
                      offsetof(catalog::Name, provenance),
                      catalog::NameProvenance::packageIdentifierCandidate)
           && valid_bool(row, offsetof(catalog::Name, strongestTierOverflow));
}

[[nodiscard]] bool valid_row(RawRow<catalog::TagName> row) noexcept {
    return valid_enum(row,
                      offsetof(catalog::TagName, provenance),
                      catalog::NameProvenance::packageIdentifierCandidate);
}

[[nodiscard]] bool valid_row(RawRow<catalog::NameCandidate> row) noexcept {
    std::uint16_t length = 0;
    return read_scalar(row, offsetof(catalog::NameCandidate, length), length)
           && length <= catalog::kNameCapacity
           && valid_enum(row,
                         offsetof(catalog::NameCandidate, provenance),
                         catalog::NameProvenance::packageIdentifierCandidate);
}

[[nodiscard]] bool valid_row(RawRow<catalog::InlineNameCandidate>) noexcept {
    return true;
}

[[nodiscard]] bool valid_row(RawRow<catalog::AuthoredSquadConfigContext> row) noexcept {
    return valid_bool(row, offsetof(catalog::AuthoredSquadConfigContext, complete));
}

[[nodiscard]] bool valid_row(RawRow<catalog::AuthoredSquadPlacementContext> row) noexcept {
    return valid_bool(row, offsetof(catalog::AuthoredSquadPlacementContext, complete));
}

[[nodiscard]] bool valid_row(RawRow<catalog::AuthoredSquadPointContext> row) noexcept {
    return valid_enum(row,
                      offsetof(catalog::AuthoredSquadPointContext, status),
                      catalog::AuthoredSquadPointContextStatus::ambiguous);
}

[[nodiscard]] bool valid_row(RawRow<catalog::AuthoredSquadPointPlacementMatch> row) noexcept {
    return valid_bool(row, offsetof(catalog::AuthoredSquadPointPlacementMatch, sameOccurrence));
}

[[nodiscard]] bool valid_row(RawRow<catalog::AuthoredSquadEdgeContext>) noexcept {
    return true;
}

[[nodiscard]] bool valid_row(RawRow<std::byte>) noexcept {
    return true;
}

/** @return True when every raw row has valid bool, enum, and bounded-string representations. */
template <typename Value>
[[nodiscard]] bool valid_rows(std::span<const std::byte> bytes, std::size_t count) noexcept {
    // A raw byte bank is capped by the file size; every typed section is capped by its row count.
    constexpr std::size_t kMaximumCount = std::is_same_v<Value, std::byte>
                                              ? format::kMaximumInlineNameBankBytes
                                              : format::kMaximumRowsPerSection;
    if (count > kMaximumCount || bytes.size() != count * sizeof(Value)) {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
        const std::span<const std::byte, sizeof(Value)> row(bytes.data() + index * sizeof(Value),
                                                            sizeof(Value));
        if (!valid_row(RawRow<Value>{row})) {
            return false;
        }
    }
    return true;
}

/** Appends one trusted fixed-layout vector and records its absolute file range. */
template <typename Value>
[[nodiscard]] bool append_section(std::vector<std::byte>& payload,
                                  format::Header& header,
                                  format::SectionIndex index,
                                  const std::vector<Value>& rows) {
    // A raw byte bank is capped by the file size; every typed section is capped by its row count.
    constexpr std::size_t kMaximumCount = std::is_same_v<Value, std::byte>
                                              ? format::kMaximumInlineNameBankBytes
                                              : format::kMaximumRowsPerSection;
    if (rows.size() > kMaximumCount) {
        return false;
    }
    const std::span<const Value> values(rows.data(), rows.size());
    const std::span<const std::byte> bytes = std::as_bytes(values);
    const std::size_t maximumPayload =
        static_cast<std::size_t>(format::kMaximumFileSize - sizeof(format::Header));
    if (payload.size() > maximumPayload || bytes.size() > maximumPayload - payload.size()) {
        return false;
    }
    format::Section& section = header.sections[static_cast<std::size_t>(index)];
    section.offset = static_cast<std::uint64_t>(sizeof(format::Header) + payload.size());
    section.count = static_cast<std::uint32_t>(rows.size());
    section.stride = static_cast<std::uint32_t>(sizeof(Value));
    payload.insert(payload.end(), bytes.begin(), bytes.end());
    return true;
}

/** Adds one trusted section's exact byte count before the payload allocates once. */
template <typename Value>
[[nodiscard]] bool add_section_size(const std::vector<Value>& rows,
                                    std::size_t& payloadSize) noexcept {
    // A raw byte bank is capped by the file size; every typed section is capped by its row count.
    constexpr std::size_t kMaximumCount = std::is_same_v<Value, std::byte>
                                              ? format::kMaximumInlineNameBankBytes
                                              : format::kMaximumRowsPerSection;
    const std::size_t maximumPayload =
        static_cast<std::size_t>(format::kMaximumFileSize - sizeof(format::Header));
    if (rows.size() > kMaximumCount || rows.size() > maximumPayload / sizeof(Value)) {
        return false;
    }
    const std::size_t bytes = rows.size() * sizeof(Value);
    if (payloadSize > maximumPayload || bytes > maximumPayload - payloadSize) {
        return false;
    }
    payloadSize += bytes;
    return true;
}

} // namespace

/** Serializes all scalar and vector fields and computes their exact payload digest. */
[[nodiscard]] bool build_payload(const Digest& sourceFingerprint,
                                 const catalog::Snapshot& snapshot,
                                 format::Header& header,
                                 std::vector<std::byte>& payload) {
    header = {};
    payload.clear();
    const format::Scalars scalars = encode_scalars(snapshot);
    std::size_t payloadSize = sizeof scalars;
    const bool sized = add_section_size(snapshot.bubbles, payloadSize)
                       && add_section_size(snapshot.states, payloadSize)
                       && add_section_size(snapshot.objects, payloadSize)
                       && add_section_size(snapshot.slots, payloadSize)
                       && add_section_size(snapshot.descriptors, payloadSize)
                       && add_section_size(snapshot.embeddedPlacementLinks, payloadSize)
                       && add_section_size(snapshot.embeddedPlacements, payloadSize)
                       && add_section_size(snapshot.references, payloadSize)
                       && add_section_size(snapshot.authoredPlacements, payloadSize)
                       && add_section_size(snapshot.containerPlacementLists, payloadSize)
                       && add_section_size(snapshot.containerPlacementOwners, payloadSize)
                       && add_section_size(snapshot.containerPlacements, payloadSize)
                       && add_section_size(snapshot.containerPlacementConfigs, payloadSize)
                       && add_section_size(snapshot.containerPlacementComponents, payloadSize)
                       && add_section_size(snapshot.type23PlacementLinks, payloadSize)
                       && add_section_size(snapshot.type23PlacementCandidates, payloadSize)
                       && add_section_size(snapshot.staticSpatialTables, payloadSize)
                       && add_section_size(snapshot.staticSpatialOwners, payloadSize)
                       && add_section_size(snapshot.staticSpatialInstances, payloadSize)
                       && add_section_size(snapshot.triggerVolumeTables, payloadSize)
                       && add_section_size(snapshot.triggerVolumeOwners, payloadSize)
                       && add_section_size(snapshot.triggerVolumeIncomingReferences, payloadSize)
                       && add_section_size(snapshot.triggerVolumeInstances, payloadSize)
                       && add_section_size(snapshot.triggerVolumeVertices, payloadSize)
                       && add_section_size(snapshot.triggerVolumeTriangles, payloadSize)
                       && add_section_size(snapshot.names, payloadSize)
                       && add_section_size(snapshot.tagNames, payloadSize)
                       && add_section_size(snapshot.nameCandidates, payloadSize)
                       && add_section_size(snapshot.inlineNameCandidates, payloadSize)
                       && add_section_size(snapshot.inlineNameBytes, payloadSize)
                       && add_section_size(snapshot.authoredSquadConfigContexts, payloadSize)
                       && add_section_size(snapshot.authoredSquadPlacementContexts, payloadSize)
                       && add_section_size(snapshot.authoredSquadPointContexts, payloadSize)
                       && add_section_size(snapshot.authoredSquadPointPlacementMatches, payloadSize)
                       && add_section_size(snapshot.authoredSquadEdgeContexts, payloadSize);
    if (!sized) {
        return false;
    }
    header.magic = format::kMagic;
    header.version = format::kVersion;
    header.headerSize = static_cast<std::uint32_t>(sizeof(format::Header));
    header.scenarioTag = snapshot.scenarioTag;
    header.sectionCount = static_cast<std::uint32_t>(format::kSectionCount);
    header.scalarSize = static_cast<std::uint32_t>(sizeof(format::Scalars));
    header.sourceFingerprint = sourceFingerprint;
    payload.reserve(payloadSize);
    payload.resize(sizeof scalars);
    std::memcpy(payload.data(), &scalars, sizeof scalars);

    const bool appended =
        append_section(payload, header, format::SectionIndex::bubbles, snapshot.bubbles)
        && append_section(payload, header, format::SectionIndex::states, snapshot.states)
        && append_section(payload, header, format::SectionIndex::objects, snapshot.objects)
        && append_section(payload, header, format::SectionIndex::slots, snapshot.slots)
        && append_section(payload, header, format::SectionIndex::descriptors, snapshot.descriptors)
        && append_section(payload,
                          header,
                          format::SectionIndex::embeddedPlacementLinks,
                          snapshot.embeddedPlacementLinks)
        && append_section(
            payload, header, format::SectionIndex::embeddedPlacements, snapshot.embeddedPlacements)
        && append_section(payload, header, format::SectionIndex::references, snapshot.references)
        && append_section(
            payload, header, format::SectionIndex::authoredPlacements, snapshot.authoredPlacements)
        && append_section(payload,
                          header,
                          format::SectionIndex::containerPlacementLists,
                          snapshot.containerPlacementLists)
        && append_section(payload,
                          header,
                          format::SectionIndex::containerPlacementOwners,
                          snapshot.containerPlacementOwners)
        && append_section(payload,
                          header,
                          format::SectionIndex::containerPlacements,
                          snapshot.containerPlacements)
        && append_section(payload,
                          header,
                          format::SectionIndex::containerPlacementConfigs,
                          snapshot.containerPlacementConfigs)
        && append_section(payload,
                          header,
                          format::SectionIndex::containerPlacementComponents,
                          snapshot.containerPlacementComponents)
        && append_section(payload,
                          header,
                          format::SectionIndex::type23PlacementLinks,
                          snapshot.type23PlacementLinks)
        && append_section(payload,
                          header,
                          format::SectionIndex::type23PlacementCandidates,
                          snapshot.type23PlacementCandidates)
        && append_section(payload,
                          header,
                          format::SectionIndex::staticSpatialTables,
                          snapshot.staticSpatialTables)
        && append_section(payload,
                          header,
                          format::SectionIndex::staticSpatialOwners,
                          snapshot.staticSpatialOwners)
        && append_section(payload,
                          header,
                          format::SectionIndex::staticSpatialInstances,
                          snapshot.staticSpatialInstances)
        && append_section(payload,
                          header,
                          format::SectionIndex::triggerVolumeTables,
                          snapshot.triggerVolumeTables)
        && append_section(payload,
                          header,
                          format::SectionIndex::triggerVolumeOwners,
                          snapshot.triggerVolumeOwners)
        && append_section(payload,
                          header,
                          format::SectionIndex::triggerVolumeIncomingReferences,
                          snapshot.triggerVolumeIncomingReferences)
        && append_section(payload,
                          header,
                          format::SectionIndex::triggerVolumeInstances,
                          snapshot.triggerVolumeInstances)
        && append_section(payload,
                          header,
                          format::SectionIndex::triggerVolumeVertices,
                          snapshot.triggerVolumeVertices)
        && append_section(payload,
                          header,
                          format::SectionIndex::triggerVolumeTriangles,
                          snapshot.triggerVolumeTriangles)
        && append_section(payload, header, format::SectionIndex::names, snapshot.names)
        && append_section(payload, header, format::SectionIndex::tagNames, snapshot.tagNames)
        && append_section(
            payload, header, format::SectionIndex::nameCandidates, snapshot.nameCandidates)
        && append_section(payload,
                          header,
                          format::SectionIndex::inlineNameCandidates,
                          snapshot.inlineNameCandidates)
        && append_section(
            payload, header, format::SectionIndex::inlineNameBytes, snapshot.inlineNameBytes)
        && append_section(payload,
                          header,
                          format::SectionIndex::authoredSquadConfigContexts,
                          snapshot.authoredSquadConfigContexts)
        && append_section(payload,
                          header,
                          format::SectionIndex::authoredSquadPlacementContexts,
                          snapshot.authoredSquadPlacementContexts)
        && append_section(payload,
                          header,
                          format::SectionIndex::authoredSquadPointContexts,
                          snapshot.authoredSquadPointContexts)
        && append_section(payload,
                          header,
                          format::SectionIndex::authoredSquadPointPlacementMatches,
                          snapshot.authoredSquadPointPlacementMatches)
        && append_section(payload,
                          header,
                          format::SectionIndex::authoredSquadEdgeContexts,
                          snapshot.authoredSquadEdgeContexts);
    if (!appended) {
        return false;
    }
    header.fileSize = static_cast<std::uint64_t>(sizeof(format::Header) + payload.size());
    return header.fileSize <= format::kMaximumFileSize
           && middleware::crypto::sha256::hash(std::span<const std::byte>(payload),
                                               header.payloadSha256);
}

/** Checks all fixed section bounds before allocating any decoded vector. */
[[nodiscard]] bool valid_shape(const format::Header& header, std::uint64_t actualSize) noexcept {
    if (header.headerSize != static_cast<std::uint32_t>(sizeof(format::Header))
        || header.fileSize != actualSize
        || header.fileSize
               < static_cast<std::uint64_t>(sizeof(format::Header) + sizeof(format::Scalars))
        || header.fileSize > format::kMaximumFileSize
        || header.sectionCount != static_cast<std::uint32_t>(format::kSectionCount)
        || header.scalarSize != static_cast<std::uint32_t>(sizeof(format::Scalars))
        || header.reserved != 0) {
        return false;
    }
    std::uint64_t priorEnd =
        static_cast<std::uint64_t>(sizeof(format::Header) + sizeof(format::Scalars));
    for (std::size_t index = 0; index < header.sections.size(); ++index) {
        const format::Section& section = header.sections[index];
        const std::uint32_t stride = kExpectedStrides[index];
        const std::uint32_t maximumCount =
            index == static_cast<std::size_t>(format::SectionIndex::inlineNameBytes)
                ? format::kMaximumInlineNameBankBytes
                : format::kMaximumRowsPerSection;
        if (section.stride != stride || section.count > maximumCount || section.offset != priorEnd
            || section.count > (std::numeric_limits<std::uint64_t>::max)() / stride) {
            return false;
        }
        const std::uint64_t bytes = static_cast<std::uint64_t>(section.count) * stride;
        if (section.offset > header.fileSize || bytes > header.fileSize - section.offset) {
            return false;
        }
        priorEnd = section.offset + bytes;
    }
    return priorEnd == header.fileSize;
}

namespace {

/** Decodes one already-bounded vector from the authenticated payload. */
template <typename Value>
[[nodiscard]] bool decode_section(const format::Header& header,
                                  format::SectionIndex index,
                                  std::span<const std::byte> payload,
                                  std::vector<Value>& output) {
    const format::Section& section = header.sections[static_cast<std::size_t>(index)];
    const std::uint64_t absoluteEnd =
        section.offset + static_cast<std::uint64_t>(section.count) * section.stride;
    if (section.offset < static_cast<std::uint64_t>(header.headerSize)
        || absoluteEnd > header.fileSize) {
        return false;
    }
    const std::size_t relative = static_cast<std::size_t>(section.offset - header.headerSize);
    const std::size_t size = static_cast<std::size_t>(absoluteEnd - section.offset);
    if (relative > payload.size() || size > payload.size() - relative) {
        return false;
    }
    const std::span<const std::byte> rows = payload.subspan(relative, size);
    if (!valid_rows<Value>(rows, section.count)) {
        return false;
    }
    output.resize(section.count);
    if (!rows.empty()) {
        std::memcpy(output.data(), rows.data(), rows.size());
    }
    return true;
}

} // namespace

/** Decodes every vector and scalar after the file layer authenticates the payload. */
bool decode_payload(const format::Header& header,
                    std::span<const std::byte> payload,
                    catalog::Snapshot& snapshot) {
    if (payload.size() < sizeof(format::Scalars)) {
        return false;
    }
    format::Scalars scalars{};
    std::memcpy(&scalars, payload.data(), sizeof scalars);
    if (!valid_scalars(scalars) || scalars.scenarioTag != header.scenarioTag) {
        return false;
    }

    catalog::Snapshot pending{};
    decode_scalars(scalars, pending);
    const bool complete =
        decode_section(header, format::SectionIndex::bubbles, payload, pending.bubbles)
        && decode_section(header, format::SectionIndex::states, payload, pending.states)
        && decode_section(header, format::SectionIndex::objects, payload, pending.objects)
        && decode_section(header, format::SectionIndex::slots, payload, pending.slots)
        && decode_section(header, format::SectionIndex::descriptors, payload, pending.descriptors)
        && decode_section(header,
                          format::SectionIndex::embeddedPlacementLinks,
                          payload,
                          pending.embeddedPlacementLinks)
        && decode_section(
            header, format::SectionIndex::embeddedPlacements, payload, pending.embeddedPlacements)
        && decode_section(header, format::SectionIndex::references, payload, pending.references)
        && decode_section(
            header, format::SectionIndex::authoredPlacements, payload, pending.authoredPlacements)
        && decode_section(header,
                          format::SectionIndex::containerPlacementLists,
                          payload,
                          pending.containerPlacementLists)
        && decode_section(header,
                          format::SectionIndex::containerPlacementOwners,
                          payload,
                          pending.containerPlacementOwners)
        && decode_section(
            header, format::SectionIndex::containerPlacements, payload, pending.containerPlacements)
        && decode_section(header,
                          format::SectionIndex::containerPlacementConfigs,
                          payload,
                          pending.containerPlacementConfigs)
        && decode_section(header,
                          format::SectionIndex::containerPlacementComponents,
                          payload,
                          pending.containerPlacementComponents)
        && decode_section(header,
                          format::SectionIndex::type23PlacementLinks,
                          payload,
                          pending.type23PlacementLinks)
        && decode_section(header,
                          format::SectionIndex::type23PlacementCandidates,
                          payload,
                          pending.type23PlacementCandidates)
        && decode_section(
            header, format::SectionIndex::staticSpatialTables, payload, pending.staticSpatialTables)
        && decode_section(
            header, format::SectionIndex::staticSpatialOwners, payload, pending.staticSpatialOwners)
        && decode_section(header,
                          format::SectionIndex::staticSpatialInstances,
                          payload,
                          pending.staticSpatialInstances)
        && decode_section(
            header, format::SectionIndex::triggerVolumeTables, payload, pending.triggerVolumeTables)
        && decode_section(
            header, format::SectionIndex::triggerVolumeOwners, payload, pending.triggerVolumeOwners)
        && decode_section(header,
                          format::SectionIndex::triggerVolumeIncomingReferences,
                          payload,
                          pending.triggerVolumeIncomingReferences)
        && decode_section(header,
                          format::SectionIndex::triggerVolumeInstances,
                          payload,
                          pending.triggerVolumeInstances)
        && decode_section(header,
                          format::SectionIndex::triggerVolumeVertices,
                          payload,
                          pending.triggerVolumeVertices)
        && decode_section(header,
                          format::SectionIndex::triggerVolumeTriangles,
                          payload,
                          pending.triggerVolumeTriangles)
        && decode_section(header, format::SectionIndex::names, payload, pending.names)
        && decode_section(header, format::SectionIndex::tagNames, payload, pending.tagNames)
        && decode_section(
            header, format::SectionIndex::nameCandidates, payload, pending.nameCandidates)
        && decode_section(header,
                          format::SectionIndex::inlineNameCandidates,
                          payload,
                          pending.inlineNameCandidates)
        && decode_section(
            header, format::SectionIndex::inlineNameBytes, payload, pending.inlineNameBytes)
        && decode_section(header,
                          format::SectionIndex::authoredSquadConfigContexts,
                          payload,
                          pending.authoredSquadConfigContexts)
        && decode_section(header,
                          format::SectionIndex::authoredSquadPlacementContexts,
                          payload,
                          pending.authoredSquadPlacementContexts)
        && decode_section(header,
                          format::SectionIndex::authoredSquadPointContexts,
                          payload,
                          pending.authoredSquadPointContexts)
        && decode_section(header,
                          format::SectionIndex::authoredSquadPointPlacementMatches,
                          payload,
                          pending.authoredSquadPointPlacementMatches)
        && decode_section(header,
                          format::SectionIndex::authoredSquadEdgeContexts,
                          payload,
                          pending.authoredSquadEdgeContexts);
    if (!complete || !valid_snapshot_graph(pending)) {
        return false;
    }
    snapshot = std::move(pending);
    return true;
}

} // namespace sunrise::state::activity_sdk::generated_world::internal
