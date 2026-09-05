#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../../core/logging/log.h"
#include "codec.h"

namespace sunrise::state::build_data::cache::records {

/** Encodes one map-package stem summary. */
bool encode(const spawn_sets::Stem& value, SpawnStemRecord& record) noexcept {
    record = {};
    if (value.nameLength == 0 || value.nameLength > spawn_sets::kStemNameCapacity
        || value.setCount == 0) {
        return false;
    }
    std::copy(value.name.begin(), value.name.end(), record.name.begin());
    record.pointCount = value.pointCount;
    record.setCount = value.setCount;
    record.nameHashOffset = value.nameHashOffset;
    record.nameHashCount = value.nameHashCount;
    record.nameLength = value.nameLength;
    return true;
}

/** Decodes one map-package stem summary. */
bool decode(const SpawnStemRecord& record, spawn_sets::Stem& value) noexcept {
    value = {};
    if (record.nameLength == 0 || record.nameLength > spawn_sets::kStemNameCapacity
        || record.setCount == 0 || record.reserved != 0) {
        return false;
    }
    std::copy(record.name.begin(), record.name.end(), value.name.begin());
    value.pointCount = record.pointCount;
    value.setCount = record.setCount;
    value.nameHashOffset = record.nameHashOffset;
    value.nameHashCount = record.nameHashCount;
    value.nameLength = record.nameLength;
    return true;
}

/**
 * Names the map-global bubbles one spawn set is offered by.
 *
 * `bubble` in an `arrival_overrides` row is read against this mask through
 * `bubbleMapIndices[bubble]`, so the row that works names a bubble whose map index has a bit here
 * -- which is not the bubble the player lands in, and is the part that reads as a bad mapping when
 * an override has to be found by trial. Printing the bits turns that from trial into a lookup.
 * @param value Finished spawn-set row.
 */
void report_offered_bubbles(const spawn_sets::NameHash& value) noexcept {
    if (!core::log::accepts(core::log::Channel::state, core::log::Level::debug)) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=build_data stage=spawn_set hash=0x%08X stem=%u points=%u "
                                "unbound=%u offered_map_bubbles=",
                                value.value,
                                static_cast<unsigned>(value.stemIndex),
                                static_cast<unsigned>(value.pointCount),
                                static_cast<unsigned>(value.unbound));
    bool first = true;
    for (std::size_t bit = 0; bit < value.bubbleMask.size() * 8 && written > 0
                              && static_cast<std::size_t>(written) + 6 < line.size();
         ++bit) {
        if ((value.bubbleMask[bit / 8] >> (bit % 8) & 1U) == 0) {
            continue;
        }
        const int more = std::snprintf(line.data() + written,
                                       line.size() - static_cast<std::size_t>(written),
                                       first ? "%zu" : ",%zu",
                                       bit);
        if (more <= 0) {
            break;
        }
        written += more;
        first = false;
    }
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

/** Encodes one distinct spawn-name hash and its point count. */
bool encode(const spawn_sets::NameHash& value, SpawnNameHashRecord& record) noexcept {
    record = {};
    if (value.pointCount == 0) {
        return false;
    }
    report_offered_bubbles(value);
    record.value = value.value;
    record.pointCount = value.pointCount;
    record.stemIndex = value.stemIndex;
    record.bubbleMask = value.bubbleMask;
    record.unbound = value.unbound;
    record.inMapPackage = value.inMapPackage;
    record.activityPackageCount = value.activityPackageCount;
    record.activityPackageOverflow = value.activityPackageOverflow;
    record.activityPackages = value.activityPackages;
    return true;
}

/** Decodes one distinct spawn-name hash and its point count. */
bool decode(const SpawnNameHashRecord& record, spawn_sets::NameHash& value) noexcept {
    value = {};
    if (record.pointCount == 0 || record.unbound > 1 || record.inMapPackage > 1
        || record.activityPackageOverflow > 1
        || record.activityPackageCount > spawn_sets::kPackageCapacity
        || record.reserved != decltype(record.reserved){}) {
        return false;
    }
    value = {record.value,
             record.pointCount,
             record.stemIndex,
             record.bubbleMask,
             record.unbound,
             record.inMapPackage,
             record.activityPackageCount,
             record.activityPackageOverflow,
             record.activityPackages};
    return true;
}

/** Encodes one spawn point. */
bool encode(const spawn_sets::Point& value, SpawnPointRecord& record) noexcept {
    record = {};
    record.position = value.position;
    record.nameHash = value.nameHash;
    record.stemIndex = value.stemIndex;
    return true;
}

/** Decodes one spawn point. */
bool decode(const SpawnPointRecord& record, spawn_sets::Point& value) noexcept {
    value = {};
    if (record.reserved != decltype(record.reserved){}) {
        return false;
    }
    value = {record.position, record.nameHash, record.stemIndex};
    return true;
}

} // namespace sunrise::state::build_data::cache::records
