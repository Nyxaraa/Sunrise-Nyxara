#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>

#include "../../../core/logging/log.h"

#include "../../../middleware/content/packages/tables/roster_intersection.h"
#include "internal.h"

namespace sunrise::client::content::scenarios {
namespace {

namespace tables = middleware::content::packages::tables;

/** Size of one publish line: the fixed fields plus the hex values that follow them. */
constexpr std::size_t kPublishLineCapacity = 192;

/**
 * Orders the safe groups the way the destination publishes them.
 * A group that binds the player or reports the lifetime comes first, then one reached through the
 * destination's own registry array, then the lower key.
 * @return True when left publishes before right.
 */
[[nodiscard]] bool publishes_first(const Candidate& left, const Candidate& right) noexcept {
    const bool leftFilled = left.bindsPlayer || left.reportsLifetime;
    const bool rightFilled = right.bindsPlayer || right.reportsLifetime;
    if (leftFilled != rightFilled) {
        return leftFilled;
    }
    if (left.primaryRegistry != right.primaryRegistry) {
        return left.primaryRegistry;
    }
    return left.key < right.key;
}

/**
 * Writes the top-level half: the candidates whose key is in every slice set.
 * @param walk Accumulator for one destination.
 * @param row Destination row receiving its group indices.
 */
void publish_top_level(Walk& walk, layouts::Definition& row) noexcept {
    std::array<std::uint32_t, tables::kRosterKeyCapacity> safe{};
    std::size_t safeCount = 0;
    if (!tables::safe_roster_keys(walk.intersection, safe, safeCount) || safeCount == 0) {
        return;
    }
    std::array<Candidate, tables::kRosterKeyCapacity> kept{};
    std::size_t keptCount = 0;
    for (std::size_t index = 0; index < walk.candidateCount; ++index) {
        const Candidate& candidate = walk.candidates[index];
        const auto last = safe.begin() + static_cast<std::ptrdiff_t>(safeCount);
        const bool keep = std::find(safe.begin(), last, candidate.key) != last;
        if (keep && keptCount < kept.size()) {
            kept[keptCount++] = candidate;
        }
    }
    std::sort(kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(keptCount), publishes_first);
    // A roster missing either filled type seeds nothing the spawn gate reads, so publish none.
    bool binds = false;
    bool reports = false;
    for (std::size_t index = 0; index < keptCount; ++index) {
        binds = binds || kept[index].bindsPlayer;
        reports = reports || kept[index].reportsLifetime;
    }
    if (!binds || !reports) {
        return;
    }
    const std::size_t published = (std::min)(keptCount, layouts::kDestinationGroupCapacity);
    for (std::size_t index = 0; index < published; ++index) {
        row.rosterGroups[index] = kept[index].group;
    }
    row.rosterGroupCount = static_cast<std::uint8_t>(published);
}

/**
 * Writes the per-bubble half: the candidates whose key is in some slice sets and not all.
 * @param walk Accumulator for one destination.
 * @param row Destination row receiving its per-bubble group indices and their bubbles.
 */
void publish_per_bubble(Walk& walk, layouts::Definition& row) noexcept {
    std::array<std::uint32_t, tables::kRosterKeyCapacity> keys{};
    std::array<std::uint64_t, tables::kRosterKeyCapacity> masks{};
    std::size_t partialCount = 0;
    if (!tables::partial_roster_keys(walk.intersection, keys, masks, partialCount)
        || partialCount == 0) {
        return;
    }
    std::size_t published = 0;
    for (std::size_t index = 0;
         index < walk.candidateCount && published < layouts::kDestinationBubbleGroupCapacity;
         ++index) {
        const Candidate& candidate = walk.candidates[index];
        for (std::size_t partial = 0; partial < partialCount; ++partial) {
            if (keys[partial] != candidate.key) {
                continue;
            }
            row.bubbleGroups[published] = candidate.group;
            row.bubbleGroupMasks[published] = masks[partial];
            ++published;
            break;
        }
    }
    row.bubbleGroupCount = static_cast<std::uint8_t>(published);
}

} // namespace

/**
 * Names every candidate and every intersection key one destination reached, and what became of it.
 * A candidate that is admitted by the slot filter and then lost in the split leaves no trace: the
 * row simply publishes fewer groups, which reads the same as a destination that never had them.
 * raid_beanstalk admits objects in bubbles 8 through 13 and 15 but publishes per-bubble groups for
 * only two of them, and nothing today says which step drops the rest.
 * @param walk Accumulator for one destination, before the split consumes it.
 * @param row Destination row being published into.
 */
void report_publish(const Walk& walk, const layouts::Definition& row) noexcept {
    if (!core::log::accepts(core::log::Channel::state, core::log::Level::debug)) {
        return;
    }
    const tables::RosterIntersection& seen = walk.intersection;
    std::array<char, kPublishLineCapacity> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=build_data stage=publish tag=0x%08X keys=%zu candidates=%zu "
                                "overflow=%u unresolved_set=%u observed=0x%llX top=%u bubble=%u",
                                row.tag,
                                seen.keyCount,
                                walk.candidateCount,
                                seen.overflowed ? 1U : 0U,
                                seen.unresolvedSet ? 1U : 0U,
                                static_cast<unsigned long long>(seen.observedSets),
                                static_cast<unsigned>(row.rosterGroupCount),
                                static_cast<unsigned>(row.bubbleGroupCount));
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    // One line per key, because the split is decided per key: a mask equal to `observed` is
    // top-level, a partial mask is per-bubble, and zero is dropped.
    for (std::size_t index = 0; index < seen.keyCount; ++index) {
        const std::uint64_t mask = seen.masks[index];
        const char* fate = mask == 0                 ? "none"
                           : mask == seen.observedSets ? "all"
                                                       : "partial";
        // A key with no candidate cannot publish: the split matches candidates against keys.
        bool paired = false;
        for (std::size_t candidate = 0; candidate < walk.candidateCount; ++candidate) {
            paired = paired || walk.candidates[candidate].key == seen.keys[index];
        }
        written = std::snprintf(line.data(),
                                line.size(),
                                "ev=build_data stage=publish_key tag=0x%08X key=0x%08X "
                                "mask=0x%llX fate=%s paired=%u",
                                row.tag,
                                seen.keys[index],
                                static_cast<unsigned long long>(mask),
                                fate,
                                paired ? 1U : 0U);
        if (written > 0) {
            core::log::write(core::log::Channel::state,
                             core::log::Level::debug,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
}

/** Splits the candidates between the destination row's two lists. */
void publish_groups(Walk& walk, layouts::Definition& row) noexcept {
    row.rosterGroupCount = 0;
    row.rosterGroups = {};
    row.bubbleGroupCount = 0;
    row.bubbleGroups = {};
    row.bubbleGroupMasks = {};
    publish_top_level(walk, row);
    // The per-bubble half is independent of the top-level one: its keys register through the
    // delta's own field 1, and a destination may reach one half and not the other.
    publish_per_bubble(walk, row);
    report_publish(walk, row);
}

} // namespace sunrise::client::content::scenarios
