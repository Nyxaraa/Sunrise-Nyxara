#include "scriptable_catalog_authored_placements.h"

#include <algorithm>

#include "../../../middleware/content/packages/tables/authored_placement_reader.h"
#include "../../../middleware/content/packages/tables/scenario_reader.h"

namespace sunrise::client::content::activity::scriptables::internal {
namespace {

namespace tables = middleware::content::packages::tables;

/** @return True when a list has already been read in this declared bubble context. */
[[nodiscard]] bool list_seen(const AuthoredPlacementAnalysis& analysis,
                             std::uint32_t objectListTag,
                             std::int32_t declaredBubbleIndex) noexcept {
    return std::any_of(analysis.lists.begin(),
                       analysis.lists.end(),
                       [objectListTag, declaredBubbleIndex](
                           const AuthoredPlacementAnalysis::List& value) noexcept {
                           return value.objectListTag == objectListTag
                                  && value.declaredBubbleIndex == declaredBubbleIndex;
                       });
}

} // namespace

/** Reads one explicit object list once for each package-declared bubble context. */
bool collect_authored_placements(AuthoredPlacementAnalysis& analysis,
                                 std::span<const std::byte> blob,
                                 std::uint32_t objectListTag,
                                 std::int32_t declaredBubbleIndex) noexcept {
    if (list_seen(analysis, objectListTag, declaredBubbleIndex)) {
        return true;
    }
    tables::Array placements{};
    if (analysis.placements.size() > kAuthoredPlacementCapacity
        || !tables::authored_placements(blob, placements)
        || placements.count > kAuthoredPlacementCapacity - analysis.placements.size()) {
        return false;
    }
    const std::size_t first = analysis.placements.size();
    try {
        analysis.placements.reserve(first + static_cast<std::size_t>(placements.count));
        for (std::uint64_t index = 0; index < placements.count; ++index) {
            tables::AuthoredPlacement source{};
            if (!tables::authored_placement_at(blob, placements, index, source)) {
                analysis.placements.resize(first);
                return false;
            }
            RawAuthoredPlacement row{};
            row.declaredBubbleIndex = declaredBubbleIndex;
            row.objectListTag = objectListTag;
            row.classListTag = source.classListTag;
            row.entryIndex = static_cast<std::uint32_t>(index);
            row.rotation = source.rotation;
            row.position = source.position;
            row.sourceOffset = static_cast<std::uint64_t>(source.sourceOffset);
            row.identifier = source.placementIdentifier;
            row.auxiliaryRelative = source.auxiliaryRelative;
            row.rotationBits = source.rotationBits;
            row.positionBits = source.positionBits;
            row.uniformScale = source.uniformScale;
            row.uniformScaleBits = source.uniformScaleBits;
            row.nameHash = source.nameHash;
            row.placementFlagsRaw = source.placementFlagsRaw;
            analysis.placements.push_back(row);
        }
        analysis.lists.push_back({declaredBubbleIndex, objectListTag});
    } catch (...) {
        analysis.placements.resize(first);
        return false;
    }
    return true;
}

/** @return True when one package-declared placement context belongs to a slice-set state. */
bool placement_applies(const RawAuthoredPlacement& placement,
                       std::uint32_t sliceSetIndex) noexcept {
    if (placement.declaredBubbleIndex == tables::kGlobalBubbleIndex) {
        return true;
    }
    return placement.declaredBubbleIndex >= 0 && sliceSetIndex % tables::kSliceSetIndexFactor == 0
           && static_cast<std::uint32_t>(placement.declaredBubbleIndex)
                  == sliceSetIndex / tables::kSliceSetIndexFactor;
}

/** Appends the anchors applicable to one exact catalog object/state placement. */
bool append_authored_placements(const AuthoredPlacementAnalysis& analysis,
                                std::uint32_t sourceObjectRow,
                                state::build_data::scriptables::Snapshot& output) noexcept {
    namespace catalog = state::build_data::scriptables;
    if (sourceObjectRow >= output.objects.size()) {
        return false;
    }
    const catalog::Object& object = output.objects[sourceObjectRow];
    if (object.stateRow >= output.states.size()) {
        return false;
    }
    const catalog::State& owner = output.states[object.stateRow];
    const std::size_t count = static_cast<std::size_t>(
        std::count_if(analysis.placements.begin(),
                      analysis.placements.end(),
                      [&owner](const RawAuthoredPlacement& value) noexcept {
                          return placement_applies(value, owner.sliceSetIndex);
                      }));
    if (output.authoredPlacements.size() > kAuthoredPlacementCapacity
        || count > kAuthoredPlacementCapacity - output.authoredPlacements.size()) {
        return false;
    }
    const std::size_t first = output.authoredPlacements.size();
    try {
        output.authoredPlacements.reserve(first + count);
        for (const RawAuthoredPlacement& source : analysis.placements) {
            if (!placement_applies(source, owner.sliceSetIndex)) {
                continue;
            }
            catalog::AuthoredPlacement row{};
            row.sourceObjectRow = sourceObjectRow;
            row.bubbleRow = object.bubbleRow;
            row.stateRow = object.stateRow;
            row.declaredBubbleIndex = source.declaredBubbleIndex;
            row.objectListTag = source.objectListTag;
            row.classListTag = source.classListTag;
            row.entryIndex = source.entryIndex;
            row.rotation = source.rotation;
            row.position = source.position;
            row.sourceOffset = source.sourceOffset;
            row.identifier = source.identifier;
            row.auxiliaryRelative = source.auxiliaryRelative;
            row.rotationBits = source.rotationBits;
            row.positionBits = source.positionBits;
            row.uniformScale = source.uniformScale;
            row.uniformScaleBits = source.uniformScaleBits;
            row.nameHash = source.nameHash;
            row.placementFlagsRaw = source.placementFlagsRaw;
            row.context = catalog::SpatialContextJoin::packageObjectState;
            output.authoredPlacements.push_back(row);
        }
    } catch (...) {
        output.authoredPlacements.resize(first);
        return false;
    }
    return true;
}

} // namespace sunrise::client::content::activity::scriptables::internal
