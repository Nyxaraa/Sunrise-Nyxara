#include "scriptable_catalog_spatial_graph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../../core/logging/log.h"
#include "../../../middleware/content/packages/tables/authored_placement_reader.h"
#include "../../../middleware/content/packages/tables/component_container_reader.h"
#include "../../../middleware/content/packages/tables/static_spatial_candidate_reader.h"
#include "../../../state/build_data/runtime.h"
#include "../spawn_sets/spawn_set_catalog_builder.h"
#include "scriptable_catalog_inline_names.h"

namespace sunrise::client::content::activity::scriptables::internal {

namespace catalog = state::build_data::scriptables;

// Fixed capacities bound one cached table's rows and inline bytes.
constexpr std::size_t kCachedTableCapacity = 2'048;
constexpr std::size_t kCachedInstanceCapacity = 2'000'000;
constexpr std::size_t kCachedInlineNameCapacity = 1'048'576;
constexpr std::size_t kCachedInlineByteCapacity = 128U * 1024U * 1024U;

/** One complete source table with row-local indices normalized for replay. */
struct CachedStaticSpatialTable final {
    std::uint32_t boundsTag{};
    std::vector<catalog::StaticSpatialInstance> instances{};
    std::vector<catalog::InlineNameCandidate> inlineNames{};
    std::vector<std::byte> inlineNameBytes{};
};

struct StaticSpatialCache::Impl final {
    Impl() {
        tables.reserve(2'048);
    }

    std::unordered_map<std::uint32_t, std::shared_ptr<const CachedStaticSpatialTable>> tables{};
    std::size_t instanceCount{};
    std::size_t inlineNameCount{};
    std::size_t inlineByteCount{};
};

/** Keeps cache storage private while the builder passes one cache through this module. */
struct StaticSpatialCacheAccess final {
    [[nodiscard]] static std::shared_ptr<const CachedStaticSpatialTable>
    find(const StaticSpatialCache& cache, std::uint32_t tableTag) noexcept {
        if (cache.impl_ == nullptr) {
            return {};
        }
        const auto found = cache.impl_->tables.find(tableTag);
        return found != cache.impl_->tables.end() ? found->second : nullptr;
    }

    /** Stores one table's decoded instances, dropping the insert when a capacity is reached. */
    static void remember(StaticSpatialCache& cache,
                         std::uint32_t tableTag,
                         CachedStaticSpatialTable value) noexcept {
        if (cache.impl_ == nullptr || find(cache, tableTag) != nullptr
            || cache.impl_->tables.size() >= kCachedTableCapacity
            || cache.impl_->instanceCount > kCachedInstanceCapacity
            || value.instances.size() > kCachedInstanceCapacity - cache.impl_->instanceCount
            || cache.impl_->inlineNameCount > kCachedInlineNameCapacity
            || value.inlineNames.size() > kCachedInlineNameCapacity - cache.impl_->inlineNameCount
            || cache.impl_->inlineByteCount > kCachedInlineByteCapacity
            || value.inlineNameBytes.size()
                   > kCachedInlineByteCapacity - cache.impl_->inlineByteCount) {
            return;
        }
        try {
            auto shared = std::make_shared<CachedStaticSpatialTable>(std::move(value));
            const std::size_t instanceCount = shared->instances.size();
            const std::size_t inlineNameCount = shared->inlineNames.size();
            const std::size_t inlineByteCount = shared->inlineNameBytes.size();
            const auto inserted = cache.impl_->tables.emplace(tableTag, std::move(shared));
            if (inserted.second) {
                cache.impl_->instanceCount += instanceCount;
                cache.impl_->inlineNameCount += inlineNameCount;
                cache.impl_->inlineByteCount += inlineByteCount;
            }
        } catch (...) {
            // A result stays valid when the optional pass cache cannot grow.
        }
    }
};

namespace {

namespace package_reader = middleware::content::packages::reader;
namespace tables = middleware::content::packages::tables;

/** Process-only limits keep one destination from consuming unbounded transient storage. */
constexpr std::size_t kTableCapacity = 8'192;
constexpr std::size_t kOwnerCapacity = 16'384;
constexpr std::size_t kInstanceCapacity = 262'144;

/** Inputs and outputs of one static spatial pass. */
struct BuildContext final {
    const package_reader::Source* source{};
    package_reader::Scratch* scratch{};
    StaticSpatialCache* cache{};
    catalog::Snapshot* output{};
    SpatialCancelCheck cancel{};
    std::string_view stem{};
    std::vector<std::byte> containerBytes{};
    std::vector<std::byte> objectListBytes{};
    std::vector<std::byte> parentBytes{};
    std::vector<std::byte> tableBytes{};
    std::vector<std::byte> boundsBytes{};
    std::vector<std::uint32_t> resources{};
    std::vector<std::uint32_t> resourceByInstance{};
    bool failed{};
};

[[nodiscard]] bool cancelled(const BuildContext& context) noexcept {
    return context.cancel != nullptr && context.cancel();
}

/** Reads one reached tag and keeps all valid inline-name evidence in its blob. */
[[nodiscard]] bool read_tag(BuildContext& context,
                            std::uint32_t tag,
                            std::vector<std::byte>& output,
                            std::uint32_t& classId) noexcept {
    if (!package_reader::read_tag(*context.source, *context.scratch, tag, output, classId)) {
        return false;
    }
    if (!collect_inline_name_evidence(*context.output, output)) {
        context.failed = true;
        return false;
    }
    return true;
}

/** Records one unresolved spatial read and names the site and tag behind it. */
void mark_unresolved(BuildContext& context, const char* site, std::uint32_t tag) noexcept {
    ++context.output->staticSpatialUnresolvedReads;
    context.output->staticSpatialComplete = false;
    std::array<char, 128> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=sdk_spatial_unresolved site=%s tag=0x%08X",
                                      site,
                                      static_cast<unsigned>(tag));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** @return True when one swept index entry belongs to the selected scenario stem. */
[[nodiscard]] bool same_stem(const BuildContext& context,
                             const ContainerIndexEntry& entry) noexcept {
    return entry.stemValid && std::string_view(entry.stem.data(), entry.stemLength) == context.stem;
}

/** Reads one package tag and checks its exact class. */
[[nodiscard]] bool read_exact(BuildContext& context,
                              std::uint32_t tag,
                              std::uint32_t expectedClass,
                              std::vector<std::byte>& output) noexcept {
    std::uint32_t classId = 0;
    if (cancelled(context) || !read_tag(context, tag, output, classId)
        || classId != expectedClass) {
        mark_unresolved(context, "read_exact", tag);
        return false;
    }
    return true;
}

/** @return The existing table row, or the row appended for this exact tag. */
[[nodiscard]] std::uint32_t table_row(BuildContext& context, std::uint32_t tag) {
    for (std::size_t row = 0; row < context.output->staticSpatialTables.size(); ++row) {
        if (cancelled(context)) {
            context.failed = true;
            return catalog::kNoRow;
        }
        if (context.output->staticSpatialTables[row].tableTag == tag) {
            return static_cast<std::uint32_t>(row);
        }
    }
    if (context.output->staticSpatialTables.size() >= kTableCapacity) {
        context.failed = true;
        return catalog::kNoRow;
    }
    catalog::StaticSpatialTable table;
    std::memset(&table, 0, sizeof table);
    table.tableTag = tag;
    table.tableNameRow = catalog::kNoRow;
    table.boundsNameRow = catalog::kNoRow;
    context.output->staticSpatialTables.push_back(table);
    return static_cast<std::uint32_t>(context.output->staticSpatialTables.size() - 1);
}

/** Checks that every candidate float is finite before it can reach projection or drawing. */
template <std::size_t Size>
[[nodiscard]] bool finite(const std::array<float, Size>& value) noexcept {
    return std::all_of(value.begin(), value.end(), [](float lane) { return std::isfinite(lane); });
}

/** @return True when all four local AABB lanes have the native minimum/maximum order. */
[[nodiscard]] bool ordered_bounds(const tables::StaticSpatialBoundsCandidate& value) noexcept {
    for (std::size_t lane = 0; lane < value.first.size(); ++lane) {
        if (value.first[lane] > value.second[lane]) {
            return false;
        }
    }
    return true;
}

/** Resolves one unique placement row from the stable package list-and-entry key. */
[[nodiscard]] std::uint32_t placement_row(BuildContext& context,
                                          std::uint32_t objectListTag,
                                          std::uint32_t objectListEntry) noexcept {
    std::uint32_t result = catalog::kNoRow;
    for (std::size_t row = 0; row < context.output->containerPlacements.size(); ++row) {
        if (cancelled(context)) {
            context.failed = true;
            return catalog::kNoRow;
        }
        const catalog::ContainerPlacement& placement = context.output->containerPlacements[row];
        if (placement.objectListTag != objectListTag || placement.entryIndex != objectListEntry) {
            continue;
        }
        if (result != catalog::kNoRow) {
            return catalog::kNoRow;
        }
        result = static_cast<std::uint32_t>(row);
    }
    return result;
}

/** Builds the ordered instance-to-resource map declared by the eight-byte rows. */
[[nodiscard]] bool build_resource_map(BuildContext& context,
                                      std::span<const std::byte> blob,
                                      const tables::StaticSpatialTable& table) {
    const auto instanceCount = static_cast<std::size_t>(table.transforms.count);
    const auto resourceCount = static_cast<std::size_t>(table.resources.count);
    context.resources.assign(resourceCount, 0);
    context.resourceByInstance.assign(instanceCount, 0);
    for (std::size_t index = 0; index < resourceCount; ++index) {
        if (cancelled(context)) {
            context.failed = true;
            return false;
        }
        if (!tables::static_spatial_resource_at(
                blob, table.resources, index, context.resources[index])) {
            return false;
        }
    }
    std::size_t expectedFirst = 0;
    for (std::size_t index = 0; index < table.ranges.count; ++index) {
        if (cancelled(context)) {
            context.failed = true;
            return false;
        }
        tables::StaticSpatialRange range{};
        if (!tables::static_spatial_range_at(blob, table.ranges, index, range)
            || range.first != expectedFirst || range.resourceIndex >= resourceCount
            || range.count > instanceCount - expectedFirst) {
            return false;
        }
        const std::uint32_t resource = context.resources[range.resourceIndex];
        std::fill(context.resourceByInstance.begin() + static_cast<std::ptrdiff_t>(expectedFirst),
                  context.resourceByInstance.begin()
                      + static_cast<std::ptrdiff_t>(expectedFirst + range.count),
                  resource);
        expectedFirst += range.count;
    }
    return expectedFirst == instanceCount;
}

/** Parses and appends every candidate row of one exact table pair. */
void materialize_table(BuildContext& context, std::uint32_t row) {
    catalog::StaticSpatialTable& destination = context.output->staticSpatialTables[row];
    tables::StaticSpatialTable source{};
    if (!tables::static_spatial_table(context.tableBytes, source)) {
        mark_unresolved(context, "table_parse", destination.tableTag);
        return;
    }
    destination.boundsTag = source.boundsTag;
    tables::Array bounds{};
    if (!read_exact(
            context, source.boundsTag, tables::kStaticSpatialBoundsTableClass, context.boundsBytes)
        || !tables::static_spatial_bounds(context.boundsBytes, bounds)
        || bounds.count != source.transforms.count) {
        mark_unresolved(context, "bounds_table", source.boundsTag);
        return;
    }
    const auto instanceCount = static_cast<std::size_t>(source.transforms.count);
    if (instanceCount > kInstanceCapacity - context.output->staticSpatialInstances.size()) {
        context.output->staticSpatialDropped += instanceCount;
        context.output->staticSpatialComplete = false;
        return;
    }
    if (!build_resource_map(context, context.tableBytes, source)) {
        if (cancelled(context)) {
            return;
        }
        mark_unresolved(context, "resource_map", destination.tableTag);
        return;
    }
    const std::size_t first = context.output->staticSpatialInstances.size();
    try {
        context.output->staticSpatialInstances.reserve(first + instanceCount);
        for (std::size_t index = 0; index < instanceCount; ++index) {
            if (cancelled(context)) {
                context.output->staticSpatialInstances.resize(first);
                context.failed = true;
                return;
            }
            tables::StaticSpatialTransformCandidate transform{};
            tables::StaticSpatialBoundsCandidate candidateBounds{};
            if (!tables::static_spatial_transform_at(
                    context.tableBytes, source.transforms, index, transform)
                || !tables::static_spatial_bounds_at(
                    context.boundsBytes, bounds, index, candidateBounds)
                || !finite(transform.first) || !finite(transform.second) || !finite(transform.third)
                || !finite(candidateBounds.first) || !finite(candidateBounds.second)
                || !ordered_bounds(candidateBounds)) {
                context.output->staticSpatialInstances.resize(first);
                mark_unresolved(context, "instance_row", destination.tableTag);
                return;
            }
            catalog::StaticSpatialInstance instance{};
            instance.tableRow = row;
            instance.instanceIndex = static_cast<std::uint32_t>(index);
            instance.resourceTag = context.resourceByInstance[index];
            instance.rotationCandidate = transform.first;
            instance.positionCandidate = transform.second;
            instance.scaleCandidate = transform.third;
            instance.localMinimum = candidateBounds.first;
            instance.localMaximum = candidateBounds.second;
            instance.boundsOpaque = candidateBounds.opaque;
            context.output->staticSpatialInstances.push_back(instance);
        }
    } catch (...) {
        context.output->staticSpatialInstances.resize(first);
        context.failed = true;
        return;
    }
    destination.firstInstance = static_cast<std::uint32_t>(first);
    destination.instanceCount = static_cast<std::uint32_t>(instanceCount);
    destination.complete = true;
}

/** Adds cached table and bounds inline strings at the skipped read boundary. */
[[nodiscard]] bool append_cached_evidence(const CachedStaticSpatialTable& cached,
                                          catalog::Snapshot& output) noexcept {
    // Snapshot rows are published as u32, so a bank stops at that maximum.
    constexpr std::size_t maximum = (std::numeric_limits<std::uint32_t>::max)();
    const std::size_t firstRow = output.inlineNameCandidates.size();
    const std::size_t firstByte = output.inlineNameBytes.size();
    if (firstRow > maximum || cached.inlineNames.size() > maximum - firstRow || firstByte > maximum
        || cached.inlineNameBytes.size() > maximum - firstByte) {
        return false;
    }
    try {
        output.inlineNameBytes.insert(output.inlineNameBytes.end(),
                                      cached.inlineNameBytes.begin(),
                                      cached.inlineNameBytes.end());
        output.inlineNameCandidates.reserve(firstRow + cached.inlineNames.size());
        for (catalog::InlineNameCandidate row : cached.inlineNames) {
            row.firstByte += static_cast<std::uint32_t>(firstByte);
            output.inlineNameCandidates.push_back(row);
        }
        return true;
    } catch (...) {
        output.inlineNameCandidates.resize(firstRow);
        output.inlineNameBytes.resize(firstByte);
        return false;
    }
}

/** Copies one complete source table and its read evidence into pass-owned storage. */
[[nodiscard]] bool capture_table(const BuildContext& context,
                                 std::uint32_t row,
                                 std::size_t firstInlineName,
                                 std::size_t firstInlineByte,
                                 CachedStaticSpatialTable& cached) noexcept {
    try {
        cached = {};
        const catalog::StaticSpatialTable& table = context.output->staticSpatialTables[row];
        cached.boundsTag = table.boundsTag;
        const auto instances = std::span(context.output->staticSpatialInstances)
                                   .subspan(table.firstInstance, table.instanceCount);
        cached.instances.assign(instances.begin(), instances.end());
        for (catalog::StaticSpatialInstance& instance : cached.instances) {
            instance.tableRow = 0;
        }
        cached.inlineNameBytes.assign(context.output->inlineNameBytes.begin()
                                          + static_cast<std::ptrdiff_t>(firstInlineByte),
                                      context.output->inlineNameBytes.end());
        cached.inlineNames.reserve(context.output->inlineNameCandidates.size() - firstInlineName);
        for (std::size_t index = firstInlineName;
             index < context.output->inlineNameCandidates.size();
             ++index) {
            catalog::InlineNameCandidate evidence = context.output->inlineNameCandidates[index];
            if (evidence.firstByte < firstInlineByte) {
                return false;
            }
            evidence.firstByte -= static_cast<std::uint32_t>(firstInlineByte);
            cached.inlineNames.push_back(evidence);
        }
        return true;
    } catch (...) {
        cached = {};
        return false;
    }
}

/** Rebases one cached table into this scenario's table and instance rows. */
void replay_table(BuildContext& context,
                  std::uint32_t row,
                  const CachedStaticSpatialTable& cached) {
    catalog::StaticSpatialTable& destination = context.output->staticSpatialTables[row];
    if (!append_cached_evidence(cached, *context.output)) {
        context.failed = true;
        return;
    }
    destination.boundsTag = cached.boundsTag;
    if (cached.instances.size()
        > kInstanceCapacity - context.output->staticSpatialInstances.size()) {
        context.output->staticSpatialDropped += cached.instances.size();
        context.output->staticSpatialComplete = false;
        return;
    }
    const std::size_t first = context.output->staticSpatialInstances.size();
    try {
        context.output->staticSpatialInstances.reserve(first + cached.instances.size());
        for (catalog::StaticSpatialInstance instance : cached.instances) {
            if (cancelled(context)) {
                context.output->staticSpatialInstances.resize(first);
                context.failed = true;
                return;
            }
            instance.tableRow = row;
            context.output->staticSpatialInstances.push_back(instance);
        }
    } catch (...) {
        context.output->staticSpatialInstances.resize(first);
        context.failed = true;
        return;
    }
    destination.firstInstance = static_cast<std::uint32_t>(first);
    destination.instanceCount = static_cast<std::uint32_t>(cached.instances.size());
    destination.complete = true;
}

/** Resolves or builds one exact candidate table. */
[[nodiscard]] std::uint32_t ensure_table(BuildContext& context, std::uint32_t tag) {
    for (std::size_t row = 0; row < context.output->staticSpatialTables.size(); ++row) {
        if (cancelled(context)) {
            context.failed = true;
            return catalog::kNoRow;
        }
        if (context.output->staticSpatialTables[row].tableTag == tag) {
            return static_cast<std::uint32_t>(row);
        }
    }
    if (context.cache != nullptr) {
        const auto cached = StaticSpatialCacheAccess::find(*context.cache, tag);
        if (cached != nullptr) {
            if (cancelled(context)) {
                mark_unresolved(context, "read_exact", tag);
                return catalog::kNoRow;
            }
            const std::uint32_t row = table_row(context, tag);
            if (row == catalog::kNoRow) {
                return row;
            }
            replay_table(context, row, *cached);
            return row;
        }
    }
    const std::size_t firstInlineName = context.output->inlineNameCandidates.size();
    const std::size_t firstInlineByte = context.output->inlineNameBytes.size();
    std::uint32_t classId = 0;
    if (!package_reader::read_tag(
            *context.source, *context.scratch, tag, context.tableBytes, classId)) {
        mark_unresolved(context, "table_read", tag);
        return catalog::kNoRow;
    }
    if (classId != tables::kStaticSpatialTableClass) {
        return catalog::kNoRow;
    }
    if (!collect_inline_name_evidence(*context.output, context.tableBytes)) {
        context.failed = true;
        return catalog::kNoRow;
    }
    const std::uint32_t row = table_row(context, tag);
    if (row == catalog::kNoRow) {
        return row;
    }
    materialize_table(context, row);
    if (context.cache != nullptr && context.output->staticSpatialTables[row].complete) {
        CachedStaticSpatialTable cached{};
        if (capture_table(context, row, firstInlineName, firstInlineByte, cached)) {
            StaticSpatialCacheAccess::remember(*context.cache, tag, std::move(cached));
        }
    }
    return row;
}

/** Adds one unique exact owner chain for a candidate table. */
void append_owner(BuildContext& context,
                  std::uint32_t tableRow,
                  std::uint32_t containerTag,
                  std::uint32_t objectListTag,
                  std::uint32_t parentTag,
                  std::uint32_t objectListEntry,
                  std::uint64_t scenarioBubbleMask,
                  const std::array<std::uint8_t, tables::kContainerBubbleMaskBytes>& mapMask) {
    for (const catalog::StaticSpatialOwner& owner : context.output->staticSpatialOwners) {
        if (cancelled(context)) {
            context.failed = true;
            return;
        }
        if (owner.tableRow == tableRow && owner.containerTag == containerTag
            && owner.objectListTag == objectListTag && owner.parentTag == parentTag
            && owner.objectListEntry == objectListEntry) {
            return;
        }
    }
    if (context.output->staticSpatialOwners.size() >= kOwnerCapacity) {
        context.failed = true;
        return;
    }
    catalog::StaticSpatialOwner owner{};
    owner.tableRow = tableRow;
    owner.placementRow = placement_row(context, objectListTag, objectListEntry);
    owner.containerTag = containerTag;
    owner.objectListTag = objectListTag;
    owner.parentTag = parentTag;
    owner.objectListEntry = objectListEntry;
    owner.scenarioBubbleMask = scenarioBubbleMask;
    owner.mapBubbleMask = mapMask;
    owner.context = catalog::SpatialContextJoin::packageStemBubble;
    if (owner.placementRow == catalog::kNoRow) {
        ++context.output->staticSpatialSemanticUnresolved;
    }
    context.output->staticSpatialOwners.push_back(owner);
}

/** Follows every typed auxiliary parent in one object-list member. */
void analyze_object_list(
    BuildContext& context,
    std::uint32_t containerTag,
    std::uint32_t objectListTag,
    std::uint64_t scenarioBubbleMask,
    const std::array<std::uint8_t, tables::kContainerBubbleMaskBytes>& mapMask) {
    tables::Array placements{};
    if (!tables::authored_placements(context.objectListBytes, placements)) {
        mark_unresolved(context, "object_list", objectListTag);
        return;
    }
    for (std::size_t index = 0; index < placements.count && !context.failed; ++index) {
        if (cancelled(context)) {
            context.failed = true;
            return;
        }
        std::uint32_t parentTag = 0;
        if (!tables::authored_placement_auxiliary_parent(
                context.objectListBytes, placements, index, parentTag)) {
            continue;
        }
        std::uint32_t parentClass = 0;
        if (!read_tag(context, parentTag, context.parentBytes, parentClass)) {
            mark_unresolved(context, "parent_read", parentTag);
            continue;
        }
        std::uint32_t tableTag = 0;
        if (!tables::static_spatial_parent_table(context.parentBytes, parentClass, tableTag)) {
            if (parentClass == tables::kStaticSpatialParentClass) {
                mark_unresolved(context, "parent_table", parentTag);
            }
            continue;
        }
        const std::uint32_t row = ensure_table(context, tableTag);
        if (row != catalog::kNoRow) {
            append_owner(context,
                         row,
                         containerTag,
                         objectListTag,
                         parentTag,
                         static_cast<std::uint32_t>(index),
                         scenarioBubbleMask,
                         mapMask);
        }
    }
}

/** @return Scenario bubble rows selected by one map-global container mask. */
[[nodiscard]] std::uint64_t scenario_bubble_mask(
    const catalog::Snapshot& output,
    const std::array<std::uint8_t, tables::kContainerBubbleMaskBytes>& mapMask) noexcept {
    std::uint64_t result = 0;
    for (const catalog::State& state : output.states) {
        if (state.bubbleRow < std::numeric_limits<std::uint64_t>::digits
            && tables::bubble_in_mask(mapMask, state.mapBubbleIndex)) {
            result |= std::uint64_t{1} << state.bubbleRow;
        }
    }
    return result;
}

/** Reads one selected-stem container and follows its spatial member chains. */
[[nodiscard]] bool collect_container_impl(BuildContext& context, std::uint32_t containerTag) {
    if (cancelled(context)) {
        return false;
    }
    if (!read_exact(context, containerTag, tables::kContainerClass, context.containerBytes)) {
        return true;
    }
    std::array<std::uint8_t, tables::kContainerBubbleMaskBytes> mapMask{};
    tables::Array members{};
    if (!tables::container_bubble_mask(context.containerBytes, mapMask)) {
        mark_unresolved(context, "container_mask", containerTag);
        return true;
    }
    if (!tables::container_members(context.containerBytes, members)) {
        mark_unresolved(context, "container_members", containerTag);
        return true;
    }
    if (members.elementClass != tables::kContainerMemberClass) {
        mark_unresolved(context, "container_member_class", containerTag);
        return true;
    }
    const std::uint64_t bubbleMask = scenario_bubble_mask(*context.output, mapMask);
    if (bubbleMask == 0) {
        return true;
    }
    for (std::size_t index = 0; index < members.count && !context.failed; ++index) {
        if (cancelled(context)) {
            context.failed = true;
            return false;
        }
        std::uint32_t memberTag = 0;
        if (!tables::container_member_at(context.containerBytes, members, index, memberTag)) {
            mark_unresolved(context, "member_at", containerTag);
            break;
        }
        std::uint32_t memberClass = 0;
        if (!read_tag(context, memberTag, context.objectListBytes, memberClass)) {
            mark_unresolved(context, "member_read", memberTag);
            continue;
        }
        if (memberClass != tables::kAuthoredPlacementListClass) {
            continue;
        }
        analyze_object_list(context, containerTag, memberTag, bubbleMask, mapMask);
    }
    return !context.failed;
}

/** Converts every allocation failure at the container boundary into one failed build. */
[[nodiscard]] bool collect_container(BuildContext& context, std::uint32_t containerTag) noexcept {
    try {
        return collect_container_impl(context, containerTag);
    } catch (...) {
        context.failed = true;
        return false;
    }
}

} // namespace

namespace {

/** Appends one result and optionally reuses complete source tables. */
[[nodiscard]] bool append_static_spatial_candidates_impl(const package_reader::Source& source,
                                                         package_reader::Scratch& scratch,
                                                         StaticSpatialCache* cache,
                                                         const ContainerIndex& containers,
                                                         std::string_view scenarioName,
                                                         catalog::Snapshot& output,
                                                         SpatialCancelCheck cancel) noexcept {
    output.staticSpatialContextResolved = false;
    output.staticSpatialNotApplicable = false;
    output.staticSpatialComplete = false;
    state::build_data::scenarios::Definition scenario{};
    if (!state::build_data::find_scenario_layout(scenarioName, scenario)) {
        ++output.staticSpatialUnresolvedReads;
        return true;
    }
    if (scenario.spawnStemLength == 0) {
        output.staticSpatialNotApplicable = true;
        output.staticSpatialComplete = true;
        return true;
    }
    try {
        const std::string_view stem(scenario.spawnStem.data(), scenario.spawnStemLength);
        BuildContext context{};
        context.source = &source;
        context.scratch = &scratch;
        context.cache = cache;
        context.output = &output;
        context.cancel = cancel;
        context.stem = stem;
        output.staticSpatialContextResolved = true;
        output.staticSpatialComplete = true;
        output.staticSpatialTables.reserve(256);
        output.staticSpatialOwners.reserve(256);
        output.staticSpatialInstances.reserve(16'384);
        // One pass-wide sweep feeds every scenario, so a refusal there is one unresolved read.
        if (!containers.complete || !containers.stemsComplete) {
            mark_unresolved(context, "container_index", 0);
        }
        for (const ContainerIndexEntry& entry : containers.entries) {
            if (context.failed || cancelled(context)) {
                break;
            }
            if (same_stem(context, entry) && !collect_container(context, entry.tag)) {
                break;
            }
        }
        return !context.failed && !cancelled(context);
    } catch (...) {
        return false;
    }
}

} // namespace

/** Allocates optional pass storage without making extraction depend on it. */
StaticSpatialCache::StaticSpatialCache() noexcept {
    try {
        impl_ = std::make_unique<Impl>();
    } catch (...) {
        impl_.reset();
    }
}

StaticSpatialCache::~StaticSpatialCache() = default;

/** Appends package spatial candidates whose stem and bubble mask match this scenario. */
bool append_static_spatial_candidates(const package_reader::Source& source,
                                      package_reader::Scratch& scratch,
                                      const ContainerIndex& containers,
                                      std::string_view scenarioName,
                                      catalog::Snapshot& output,
                                      SpatialCancelCheck cancel) noexcept {
    return append_static_spatial_candidates_impl(
        source, scratch, nullptr, containers, scenarioName, output, cancel);
}

/** Appends package spatial candidates with pass-local table reuse. */
bool append_static_spatial_candidates(const package_reader::Source& source,
                                      package_reader::Scratch& scratch,
                                      StaticSpatialCache& cache,
                                      const ContainerIndex& containers,
                                      std::string_view scenarioName,
                                      catalog::Snapshot& output,
                                      SpatialCancelCheck cancel) noexcept {
    return append_static_spatial_candidates_impl(
        source, scratch, &cache, containers, scenarioName, output, cancel);
}

} // namespace sunrise::client::content::activity::scriptables::internal
