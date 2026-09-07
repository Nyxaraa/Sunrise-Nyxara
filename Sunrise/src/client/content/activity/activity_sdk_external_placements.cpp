#include "activity_sdk_external_placements.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>

namespace sunrise::client::content::activity::sdk_generation::external_placements {
namespace {

/** One install holds about 211,000 distinct rows, so this bounds a malformed one. */
constexpr std::size_t kRowCapacity = 2'097'152;
/** One destination's rows repeat per scenario, which is what makes this bound the larger one. */
constexpr std::size_t kScenarioRowCapacity = 16'777'216;

[[nodiscard]] bool usable_identity(std::uint64_t identity) noexcept {
    return identity != 0 && identity != (std::numeric_limits<std::uint64_t>::max)();
}

/** Packs one float triple into the bit form every placement row already carries. */
[[nodiscard]] std::array<std::uint32_t, 3> bits3(const std::array<float, 3>& value) noexcept {
    return {std::bit_cast<std::uint32_t>(value[0]),
            std::bit_cast<std::uint32_t>(value[1]),
            std::bit_cast<std::uint32_t>(value[2])};
}

[[nodiscard]] std::array<std::uint32_t, 4> bits4(const std::array<float, 4>& value) noexcept {
    return {std::bit_cast<std::uint32_t>(value[0]),
            std::bit_cast<std::uint32_t>(value[1]),
            std::bit_cast<std::uint32_t>(value[2]),
            std::bit_cast<std::uint32_t>(value[3])};
}

[[nodiscard]] RowKey key_of(const Row& row) noexcept {
    return {row.placedEntryIdentity, row.positionBits, row.quaternionBits, row.uniformScaleBits};
}

/** Interns one row and records that this scenario carries it. */
[[nodiscard]] bool retain(std::uint32_t scenarioTag, const Row& row, Index& output) noexcept {
    if (!std::isfinite(std::bit_cast<float>(row.uniformScaleBits))) {
        return true;
    }
    if (output.scenarioRows.size() >= kScenarioRowCapacity) {
        output.complete = false;
        return false;
    }
    try {
        const RowKey key = key_of(row);
        auto found = output.lookup.find(key);
        if (found == output.lookup.end()) {
            if (output.rows.size() >= kRowCapacity) {
                output.complete = false;
                return false;
            }
            found =
                output.lookup.emplace(key, static_cast<std::uint32_t>(output.rows.size())).first;
            output.rows.push_back(row);
        }
        output.scenarioRows.push_back({scenarioTag, found->second});
    } catch (...) {
        output.complete = false;
        return false;
    }
    return true;
}

} // namespace

/** Mixes the identity and every transform lane, because one identity holds several transforms. */
std::size_t RowKeyHash::operator()(const RowKey& value) const noexcept {
    std::size_t seed = std::hash<std::uint64_t>{}(value.placedEntryIdentity);
    const auto mix = [&seed](std::uint32_t word) noexcept {
        seed ^=
            static_cast<std::size_t>(word) + 0x9E3779B97F4A7C15ULL + (seed << 6U) + (seed >> 2U);
    };
    for (const std::uint32_t word : value.positionBits) {
        mix(word);
    }
    for (const std::uint32_t word : value.quaternionBits) {
        mix(word);
    }
    mix(value.uniformScaleBits);
    return seed;
}

/** Harvests one scenario's container and descriptor-embedded placements into the index. */
bool append(std::uint32_t scenarioTag, const catalog::Snapshot& snapshot, Index& output) noexcept {
    for (const catalog::ContainerPlacement& placement : snapshot.containerPlacements) {
        if (!placement.complete || !placement.placementIdentifierRead
            || !usable_identity(placement.placementIdentifier)) {
            continue;
        }
        Row row{};
        row.placedEntryIdentity = placement.placementIdentifier;
        row.objectListTag = placement.objectListTag;
        row.placementOrdinal = placement.entryIndex;
        row.classListTag = placement.classListTag;
        row.positionBits = bits3(placement.position);
        row.quaternionBits = bits4(placement.rotation);
        row.uniformScaleBits = std::bit_cast<std::uint32_t>(placement.uniformScale);
        if (!retain(scenarioTag, row, output)) {
            return false;
        }
    }
    for (const catalog::EmbeddedPlacement& placement : snapshot.embeddedPlacements) {
        if (!usable_identity(placement.identifier)
            || placement.linkRow >= snapshot.embeddedPlacementLinks.size()
            || !snapshot.embeddedPlacementLinks[placement.linkRow].complete) {
            continue;
        }
        Row row{};
        row.placedEntryIdentity = placement.identifier;
        // A descriptor-embedded row belongs to no object list, so the tag stays zero.
        row.objectListTag = 0;
        row.placementOrdinal = placement.entryIndex;
        row.classListTag = placement.classListTag;
        row.nameHash = placement.nameHash;
        row.positionBits = bits3(placement.position);
        row.quaternionBits = bits4(placement.rotation);
        // The embedded row carries no uniform scale, so the anchor takes the identity scale.
        row.uniformScaleBits = std::bit_cast<std::uint32_t>(1.0F);
        if (!retain(scenarioTag, row, output)) {
            return false;
        }
    }
    return true;
}

/** Sorts the scenario references, drops repeats, and releases the pass-local lookup. */
void finalize(Index& output) noexcept {
    output.lookup.clear();
    std::sort(output.scenarioRows.begin(),
              output.scenarioRows.end(),
              [](const ScenarioRow& first, const ScenarioRow& second) {
                  return std::tie(first.scenarioTag, first.rowIndex)
                         < std::tie(second.scenarioTag, second.rowIndex);
              });
    output.scenarioRows.erase(std::unique(output.scenarioRows.begin(),
                                          output.scenarioRows.end(),
                                          [](const ScenarioRow& first, const ScenarioRow& second) {
                                              return first.scenarioTag == second.scenarioTag
                                                     && first.rowIndex == second.rowIndex;
                                          }),
                              output.scenarioRows.end());
}

/** The sorted references one scenario tag owns. */
std::span<const ScenarioRow> scenario_rows(const Index& index, std::uint32_t scenarioTag) noexcept {
    const auto first = std::lower_bound(
        index.scenarioRows.begin(),
        index.scenarioRows.end(),
        scenarioTag,
        [](const ScenarioRow& row, std::uint32_t tag) { return row.scenarioTag < tag; });
    const auto last = std::upper_bound(
        first,
        index.scenarioRows.end(),
        scenarioTag,
        [](std::uint32_t tag, const ScenarioRow& row) { return tag < row.scenarioTag; });
    return {index.scenarioRows.data() + (first - index.scenarioRows.begin()),
            static_cast<std::size_t>(last - first)};
}

} // namespace sunrise::client::content::activity::sdk_generation::external_placements
