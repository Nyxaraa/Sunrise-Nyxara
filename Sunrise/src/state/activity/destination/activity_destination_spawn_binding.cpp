#include "activity_destination_spawn_binding.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../middleware/content/packages/tables/region_reader.h"
#include "../../build_data/runtime.h"

namespace sunrise::state::activity::destination {
namespace {

/** Spawn-set rows read for one stem. The widest installed stem declares 294. */
constexpr std::size_t kSpawnRowCapacity = 512;

/** Last reported hash, so a per-push decision is written once. */
std::atomic_uint32_t g_reportedHash{};

/** @return The destination's package name as a bounded view. */
[[nodiscard]] std::string_view name_of(const DestinationSelection& selection) noexcept {
    return {reinterpret_cast<const char*>(selection.packageName.data()),
            selection.packageNameLength};
}

/**
 * Tests whether a destination loads the package that declares one set.
 * @param layout Destination row carrying the packages it loads.
 * @param row Spawn-set row carrying the packages that declare it.
 * @return True when the set is in the map package or in one the destination names.
 */
[[nodiscard]] bool loads_package(const build_data::scenarios::Definition& layout,
                                 const build_data::spawn_sets::NameHash& row) noexcept {
    if (row.inMapPackage != 0) {
        return true;
    }
    const std::size_t declared =
        layout.packageCount < layout.packages.size() ? layout.packageCount : layout.packages.size();
    for (std::size_t index = 0; index < row.activityPackageCount; ++index) {
        for (std::size_t package = 0; package < declared; ++package) {
            if (layout.packages[package] == row.activityPackages[index]) {
                return true;
            }
        }
    }
    return false;
}

/** Writes the drop once per hash. @param name Destination the set was dropped for. */
void report_dropped(std::string_view name, std::uint32_t hash) noexcept {
    if (g_reportedHash.exchange(hash, std::memory_order_acq_rel) == hash) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=spawn_set result=dropped name=%.*s "
                                      "spawn=0x%08X reason=not_loaded",
                                      static_cast<int>(name.size()),
                                      name.data(),
                                      hash);
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/**
 * Tests whether one bubble of a destination is named by a spawn row's bubble mask.
 * The mask is keyed by map-global bubble index, so the destination's own ordinal has to be
 * translated through its map-index table first.
 * @param layout Destination row carrying the map-index table.
 * @param row Spawn-set row carrying the mask.
 * @param bubble Destination bubble ordinal.
 * @return True when the row declares that bubble.
 */
[[nodiscard]] bool bubble_declares_set(const build_data::scenarios::Definition& layout,
                                       const build_data::spawn_sets::NameHash& row,
                                       std::size_t bubble) noexcept {
    if (bubble >= layout.bubbleCount || bubble >= layout.bubbleMapIndices.size()) {
        return false;
    }
    const std::size_t mapIndex = layout.bubbleMapIndices[bubble];
    const std::size_t byteIndex = mapIndex / 8;
    if (byteIndex >= row.bubbleMask.size()) {
        return false;
    }
    return (row.bubbleMask[byteIndex] >> (mapIndex % 8) & 1U) != 0;
}

} // namespace

/** Finds the slice set whose bubble actually declares one spawn set. */
std::uint16_t spawn_set_slice_set(const DestinationSelection& selection,
                                  std::uint32_t spawnSetHash,
                                  std::uint16_t arrivalSliceSet) noexcept {
    namespace tables = middleware::content::packages::tables;
    if (spawnSetHash == 0 || spawnSetHash == kAbsentSpawnSetHash) {
        return arrivalSliceSet;
    }
    const std::string_view name = name_of(selection);
    build_data::scenarios::Definition layout{};
    if (name.empty() || !build_data::find_scenario_layout(name, layout)) {
        return arrivalSliceSet;
    }
    const std::string_view stem(layout.spawnStem.data(), layout.spawnStemLength);
    static std::array<build_data::spawn_sets::NameHash, kSpawnRowCapacity> rows{};
    std::size_t count = 0;
    if (stem.empty() || !build_data::find_spawn_sets(stem, rows, count)) {
        return arrivalSliceSet;
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (rows[index].value != spawnSetHash) {
            continue;
        }
        // The arrival wins whenever it is valid, so every configuration that already places a
        // player keeps the exact slice set it publishes today.
        const std::size_t arrivalBubble = arrivalSliceSet / tables::kSliceSetIndexFactor;
        if (bubble_declares_set(layout, rows[index], arrivalBubble)) {
            return arrivalSliceSet;
        }
        const std::size_t declared = layout.bubbleCount < layout.bubbleMapIndices.size()
                                         ? layout.bubbleCount
                                         : layout.bubbleMapIndices.size();
        for (std::size_t bubble = 0; bubble < declared; ++bubble) {
            if (bubble_declares_set(layout, rows[index], bubble)) {
                return static_cast<std::uint16_t>(
                    tables::region_index(static_cast<std::uint32_t>(bubble)));
            }
        }
        return arrivalSliceSet;
    }
    // A hash no row carries is not proof of a miss: the row set can be capped.
    return arrivalSliceSet;
}

/** Drops a spawn set the destination cannot load. Only a proved miss is dropped. */
std::uint32_t attachable_spawn_set_hash(const DestinationSelection& selection,
                                        std::uint32_t fallback) noexcept {
    const std::uint32_t hash = resolve_spawn_set_hash(selection, fallback);
    if (hash == 0 || hash == kAbsentSpawnSetHash) {
        return hash;
    }
    const std::string_view name = name_of(selection);
    build_data::scenarios::Definition layout{};
    if (name.empty() || !build_data::find_scenario_layout(name, layout)) {
        return hash;
    }
    const std::string_view stem(layout.spawnStem.data(), layout.spawnStemLength);
    static std::array<build_data::spawn_sets::NameHash, kSpawnRowCapacity> rows{};
    std::size_t count = 0;
    if (stem.empty() || !build_data::find_spawn_sets(stem, rows, count)) {
        return hash;
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (rows[index].value != hash) {
            continue;
        }
        if (loads_package(layout, rows[index])) {
            g_reportedHash.store(0, std::memory_order_release);
            return hash;
        }
        report_dropped(name, hash);
        return kAbsentSpawnSetHash;
    }
    // A hash no row carries is not proof of a miss: the row set can be capped. Send it as picked.
    return hash;
}

} // namespace sunrise::state::activity::destination
