#include <cstring>
#include <limits>

#include "../../../../state/build_data/runtime.h"
#include "../../../../state/build_data/sobjects/sobject_catalog.h"
#include "internal.h"

namespace sunrise::client::content::items::packages {

/** Reads the fixed collectible table and publishes its native-index item links. */
bool build_collectibles(const reader::Source& source,
                        Storage& storage,
                        std::span<const std::byte> root,
                        std::uint64_t itemDefinitionCount) noexcept {
    namespace domain = state::build_data::collectibles;

    // The definition table an incident target names. Read here because this pass already holds an
    // open source, and because a collectible picked up in the world arrives as an incident: without
    // this table its target is a bare number.
    if (state::build_data::sobjects::count() == 0) {
        namespace sobjects = state::build_data::sobjects;
        // Count at +112; 40-byte rows at +128 hold the name hash, packed lane, and type.
        constexpr std::size_t kCountOffset = 112;
        constexpr std::size_t kRowBase = 128;
        constexpr std::size_t kRowStride = 40;
        for (const std::uint32_t tag : {0x81327CD4U, 0x80B9E5BFU}) {
            std::vector<std::byte> blob{};
            if (!reader::read_tag(source, storage.scratch, tag, blob) || blob.size() < kRowBase) {
                continue;
            }
            std::uint64_t rowCount = 0;
            std::memcpy(&rowCount, blob.data() + kCountOffset, sizeof rowCount);
            if (rowCount == 0 || rowCount > sobjects::kDefinitionCapacity
                || kRowBase + static_cast<std::size_t>(rowCount) * kRowStride > blob.size()) {
                continue;
            }
            std::vector<sobjects::Definition> rows(static_cast<std::size_t>(rowCount));
            for (std::size_t row = 0; row < rows.size(); ++row) {
                const std::size_t at = kRowBase + row * kRowStride;
                std::memcpy(&rows[row].nameHash, blob.data() + at, sizeof rows[row].nameHash);
                std::memcpy(&rows[row].lane4, blob.data() + at + 16, sizeof rows[row].lane4);
                std::memcpy(&rows[row].typeCode, blob.data() + at + 36, sizeof rows[row].typeCode);
            }
            (void)sobjects::replace(std::span<const sobjects::Definition>{rows});
            break;
        }
    }

    if (state::build_data::collectible_definitions_ready()) {
        return true;
    }
    if (itemDefinitionCount == 0
        || itemDefinitionCount > state::build_data::items::kDefinitionCapacity) {
        return false;
    }

    std::uint32_t requirementTableTag = 0;
    std::uint32_t requirementTableClass = 0;
    tables::Array requirementSets{};
    if (!tables::slot_tag(root, tables::kMaterialRequirementTableSlot, requirementTableTag)
        || requirementTableTag == 0
        || tables::package_of(requirementTableTag) == tables::kAbsentPackageId
        || !reader::read_tag(
            source, storage.scratch, requirementTableTag, storage.definition, requirementTableClass)
        || requirementTableClass != tables::kMaterialRequirementTableClass
        || !tables::find_array_at(std::span<const std::byte>{storage.definition},
                                  tables::kTableArrayDescriptor,
                                  requirementSets)
        || requirementSets.elementClass != tables::kMaterialRequirementSetRowClass
        || requirementSets.count == 0
        || requirementSets.count > (std::numeric_limits<std::uint16_t>::max)()) {
        return false;
    }
    const std::span<const std::byte> requirementTable{storage.definition};
    if (requirementSets.dataOffset > requirementTable.size()
        || requirementSets.count > (requirementTable.size() - requirementSets.dataOffset)
                                       / tables::kMaterialRequirementSetRowStride) {
        return false;
    }

    std::uint32_t tableTag = 0;
    std::uint32_t tableClass = 0;
    tables::Array rows{};
    if (!tables::slot_tag(root, tables::kCollectibleTableSlot, tableTag) || tableTag == 0
        || tables::package_of(tableTag) == tables::kAbsentPackageId
        || !reader::read_tag(source, storage.scratch, tableTag, storage.child, tableClass)
        || tableClass != tables::kCollectibleTableClass
        || !tables::find_array_at(
            std::span<const std::byte>{storage.child}, tables::kTableArrayDescriptor, rows)
        || rows.elementClass != tables::kCollectibleRowClass || rows.count == 0
        || rows.count > storage.collectibleRows.size()) {
        return false;
    }

    const std::span<const std::byte> table{storage.child};
    if (rows.dataOffset > table.size()
        || rows.count > (table.size() - rows.dataOffset) / tables::kCollectibleRowStride) {
        return false;
    }
    for (std::uint64_t row = 0; row < rows.count; ++row) {
        const std::size_t at =
            rows.dataOffset + static_cast<std::size_t>(row) * tables::kCollectibleRowStride;
        std::uint32_t collectibleHash = 0;
        std::uint16_t itemDefinitionIndex = domain::kUnavailableItemDefinitionIndex;
        std::uint16_t requirementSetIndex = domain::kUnavailableMaterialRequirementSetIndex;
        std::memcpy(&collectibleHash,
                    table.data() + at + tables::kCollectibleHashOffset,
                    sizeof collectibleHash);
        std::memcpy(&itemDefinitionIndex,
                    table.data() + at + tables::kCollectibleItemIndexOffset,
                    sizeof itemDefinitionIndex);
        std::memcpy(&requirementSetIndex,
                    table.data() + at + tables::kCollectibleMaterialRequirementIndexOffset,
                    sizeof requirementSetIndex);
        if (itemDefinitionIndex != domain::kUnavailableItemDefinitionIndex
            && itemDefinitionIndex >= itemDefinitionCount) {
            return false;
        }
        domain::Definition& output = storage.collectibleRows[static_cast<std::size_t>(row)];
        output = {};
        output.collectibleHash = collectibleHash;
        output.collectibleIndex = static_cast<std::uint16_t>(row);
        output.itemDefinitionIndex = itemDefinitionIndex;
        if (requirementSetIndex == domain::kUnavailableMaterialRequirementSetIndex) {
            continue;
        }
        if (requirementSetIndex >= requirementSets.count) {
            return false;
        }
        const std::size_t setAt = requirementSets.dataOffset
                                  + static_cast<std::size_t>(requirementSetIndex)
                                        * tables::kMaterialRequirementSetRowStride;
        std::uint32_t requirementSetHash = 0;
        std::int64_t descriptorRelative = 0;
        std::memcpy(&requirementSetHash,
                    requirementTable.data() + setAt + tables::kMaterialRequirementSetHashOffset,
                    sizeof requirementSetHash);
        std::memcpy(&descriptorRelative,
                    requirementTable.data() + setAt
                        + tables::kMaterialRequirementSetArrayPointerOffset,
                    sizeof descriptorRelative);
        const std::size_t pointerAt = setAt + tables::kMaterialRequirementSetArrayPointerOffset;
        if (requirementSetHash == 0 || descriptorRelative < -static_cast<std::int64_t>(pointerAt)
            || descriptorRelative
                   > static_cast<std::int64_t>(requirementTable.size() - pointerAt)) {
            return false;
        }
        const auto descriptorSigned = static_cast<std::int64_t>(pointerAt) + descriptorRelative;
        if (descriptorSigned < 0) {
            return false;
        }
        tables::Array requirements{};
        if (!tables::find_array_at(
                requirementTable, static_cast<std::size_t>(descriptorSigned), requirements)
            || requirements.elementClass != tables::kMaterialRequirementRowClass
            || requirements.count == 0 || requirements.count > output.materialRequirements.size()
            || requirements.dataOffset > requirementTable.size()
            || requirements.count > (requirementTable.size() - requirements.dataOffset)
                                        / tables::kMaterialRequirementRowStride) {
            return false;
        }
        output.materialRequirementSetHash = requirementSetHash;
        output.materialRequirementSetIndex = requirementSetIndex;
        output.materialRequirementCount = static_cast<std::uint8_t>(requirements.count);
        for (std::size_t requirement = 0; requirement < requirements.count; ++requirement) {
            const std::size_t requirementAt =
                requirements.dataOffset + requirement * tables::kMaterialRequirementRowStride;
            std::uint32_t nativeItemIndex = 0;
            std::uint32_t quantity = 0;
            std::uint8_t deleteOnAction = 0;
            std::uint8_t omitFromRequirements = 0;
            std::uint16_t sentinel = 0;
            std::memcpy(&nativeItemIndex,
                        requirementTable.data() + requirementAt
                            + tables::kMaterialRequirementItemIndexOffset,
                        sizeof nativeItemIndex);
            std::memcpy(&quantity,
                        requirementTable.data() + requirementAt
                            + tables::kMaterialRequirementQuantityOffset,
                        sizeof quantity);
            std::memcpy(&deleteOnAction,
                        requirementTable.data() + requirementAt
                            + tables::kMaterialRequirementDeleteOffset,
                        sizeof deleteOnAction);
            std::memcpy(&omitFromRequirements,
                        requirementTable.data() + requirementAt
                            + tables::kMaterialRequirementOmitOffset,
                        sizeof omitFromRequirements);
            std::memcpy(&sentinel,
                        requirementTable.data() + requirementAt
                            + tables::kMaterialRequirementSentinelOffset,
                        sizeof sentinel);
            if (nativeItemIndex >= itemDefinitionCount
                || nativeItemIndex > (std::numeric_limits<std::uint16_t>::max)() || quantity == 0
                || deleteOnAction > 1 || omitFromRequirements > 1 || sentinel != 0xFFFFU) {
                return false;
            }
            output.materialRequirements[requirement] = {
                quantity,
                static_cast<std::uint16_t>(nativeItemIndex),
                deleteOnAction != 0,
                omitFromRequirements != 0,
            };
        }
    }
    return state::build_data::publish_collectible_definitions(
        std::span(storage.collectibleRows).first(static_cast<std::size_t>(rows.count)));
}

} // namespace sunrise::client::content::items::packages
