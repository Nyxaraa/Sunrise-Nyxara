#include "scriptable_catalog_type23_placement_links.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <array>
#include <cstdio>
#include <vector>

#include "../../../core/logging/log.h"
#include "../../../middleware/content/packages/tables/type23_placement_identifier_reader.h"
#include "../../../state/build_data/scriptables/scriptable_catalog.h"

namespace sunrise::client::content::activity::scriptables::internal {
namespace {

namespace catalog = state::build_data::scriptables;
namespace tables = middleware::content::packages::tables;

constexpr std::size_t kLinkCapacity = 262'144;
constexpr std::size_t kCandidateCapacity = 1'048'576;

struct PlacementIndex final {
    std::uint64_t identifier{};
    std::uint32_t placementRow{};
};

[[nodiscard]] bool cancelled(Type23PlacementCancelCheck check) noexcept {
    return check != nullptr && check();
}

/** @return True when the selected-stem placement and owner universe is complete. */
[[nodiscard]] bool identity_owner_inventory_complete(const catalog::Snapshot& output) noexcept {
    const catalog::ContainerPlacementDiagnostics& diagnostics =
        output.containerPlacementDiagnostics;
    return diagnostics.contextNotApplicable
           || (diagnostics.contextResolved && diagnostics.identityOwnerInventoryComplete);
}

/** Adds a bounded diagnostic count without allowing wrap. */
void add_count(std::uint64_t& destination, std::size_t count = 1) noexcept {
    const std::uint64_t available = (std::numeric_limits<std::uint64_t>::max)() - destination;
    destination += (std::min)(available, static_cast<std::uint64_t>(count));
}

/** @return The owning object row for one exact descriptor, when all adjacencies agree. */
[[nodiscard]] const catalog::Object*
descriptor_object(const catalog::Snapshot& source, const catalog::Descriptor& descriptor) noexcept {
    if (descriptor.slotRow >= source.slots.size()) {
        return nullptr;
    }
    const catalog::Slot& slot = source.slots[descriptor.slotRow];
    if (slot.objectRow >= source.objects.size() || slot.slotType != 23) {
        return nullptr;
    }
    const catalog::Object& object = source.objects[slot.objectRow];
    const std::size_t first = object.firstSlot;
    const std::size_t count = object.slotCount;
    return first <= descriptor.slotRow && count <= source.slots.size() - first
                   && descriptor.slotRow - first < count
               ? &object
               : nullptr;
}

/** Counts applicable owners and retains the lowest exact owner row for provenance. */
[[nodiscard]] std::uint32_t applicable_owners(const catalog::Snapshot& source,
                                              const catalog::ContainerPlacement& placement,
                                              std::uint32_t bubbleRow,
                                              std::uint32_t& retainedOwner) noexcept {
    retainedOwner = catalog::kNoRow;
    std::uint32_t count = 0;
    for (std::size_t row = 0; row < source.containerPlacementOwners.size(); ++row) {
        const catalog::ContainerPlacementOwner& owner = source.containerPlacementOwners[row];
        if (owner.listRow != placement.listRow
            || !catalog::container_placement_owner_applies(owner, bubbleRow)) {
            continue;
        }
        if (count == 0) {
            retainedOwner = static_cast<std::uint32_t>(row);
        }
        if (count != (std::numeric_limits<std::uint32_t>::max)()) {
            ++count;
        }
    }
    return count;
}

/**
 * Counts owners whose container loads anywhere in this scenario, ignoring the sensor's own bubble.
 * The client binds by scanning live objects for the identity. So the driven object need not share
 * a bubble with the sensor: geometry ships in the map package, the sensor in the activity one.
 */
[[nodiscard]] std::uint32_t scenario_owners(const catalog::Snapshot& source,
                                            const catalog::ContainerPlacement& placement,
                                            std::uint32_t& retainedOwner) noexcept {
    retainedOwner = catalog::kNoRow;
    std::uint32_t count = 0;
    for (std::size_t row = 0; row < source.containerPlacementOwners.size(); ++row) {
        const catalog::ContainerPlacementOwner& owner = source.containerPlacementOwners[row];
        if (owner.listRow != placement.listRow
            || owner.context != catalog::SpatialContextJoin::packageStemBubble
            || owner.scenarioBubbleMask == 0) {
            continue;
        }
        if (count == 0) {
            retainedOwner = static_cast<std::uint32_t>(row);
        }
        if (count != (std::numeric_limits<std::uint32_t>::max)()) {
            ++count;
        }
    }
    return count;
}

/**
 * Records every type-23 device whose placement link did not resolve to one exact placement.
 *
 * A device that resolves to no exact placement is never drawn and has nothing in the world to
 * drive, so its Auth is accepted and does nothing. Nothing surfaced that before: the browser
 * shows such a row as "context only" whenever its owning object has context positions, which
 * hides the failed join behind the object's own count. The builder already counts every way the
 * match can fail, so this reports the counters once and then names the unresolved rows.
 */
void report_unresolved_links(const catalog::Snapshot& output) noexcept {
    const auto& diagnostics = output.type23PlacementDiagnostics;
    std::array<char, 224> line{};
    int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=type23_placement stage=summary links=%zu complete=%d unread=%llu zero_identity=%llu "
        "multi_identity=%llu zero_active=%llu multi_active=%llu scenario_resolved=%llu",
        output.type23PlacementLinks.size(),
        diagnostics.complete ? 1 : 0,
        static_cast<unsigned long long>(diagnostics.unreadIdentifiers),
        static_cast<unsigned long long>(diagnostics.zeroIdentityMatches),
        static_cast<unsigned long long>(diagnostics.multipleIdentityMatches),
        static_cast<unsigned long long>(diagnostics.zeroActiveCandidates),
        static_cast<unsigned long long>(diagnostics.multipleActiveCandidates),
        static_cast<unsigned long long>(diagnostics.scenarioResolvedCandidates));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    std::size_t reported = 0;
    for (const catalog::Type23PlacementLink& link : output.type23PlacementLinks) {
        if (link.join == catalog::ReferenceJoin::exact || reported >= 32) {
            continue;
        }
        ++reported;
        written = std::snprintf(line.data(),
                                line.size(),
                                "ev=type23_placement stage=unresolved slot=%u identifier=%llu "
                                "identity_matches=%u candidates=%u active=%u complete=%d join=%d",
                                link.slotRow,
                                static_cast<unsigned long long>(link.placementIdentifier),
                                link.identityMatchCount,
                                link.candidateCount,
                                link.activeCandidateCount,
                                link.complete ? 1 : 0,
                                static_cast<int>(link.join));
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
}

/** Appends one descriptor link and every retained exact identifier match. */
[[nodiscard]] bool append_link(catalog::Snapshot& output,
                               std::uint32_t descriptorRow,
                               std::span<const PlacementIndex> index) {
    catalog::Descriptor& descriptor = output.descriptors[descriptorRow];
    catalog::Type23PlacementLink link{};
    link.descriptorRow = descriptorRow;
    link.slotRow = descriptor.slotRow;
    link.placementIdentifier = descriptor.placementIdentifier;
    link.firstCandidate = static_cast<std::uint32_t>(output.type23PlacementCandidates.size());
    const std::uint32_t linkRow = static_cast<std::uint32_t>(output.type23PlacementLinks.size());
    descriptor.placementLinkRow = linkRow;
    if (!descriptor.placementIdentifierRead) {
        ++output.type23PlacementDiagnostics.unreadIdentifiers;
        output.type23PlacementDiagnostics.complete = false;
        output.type23PlacementLinks.push_back(link);
        return true;
    }

    const auto first =
        std::lower_bound(index.begin(),
                         index.end(),
                         descriptor.placementIdentifier,
                         [](const PlacementIndex& row, std::uint64_t identifier) noexcept {
                             return row.identifier < identifier;
                         });
    const auto last =
        std::upper_bound(first,
                         index.end(),
                         descriptor.placementIdentifier,
                         [](std::uint64_t identifier, const PlacementIndex& row) noexcept {
                             return identifier < row.identifier;
                         });
    const std::size_t identityMatches = static_cast<std::size_t>(last - first);
    link.identityMatchCount = static_cast<std::uint32_t>(identityMatches);
    if (identityMatches == 0) {
        ++output.type23PlacementDiagnostics.zeroIdentityMatches;
    } else if (identityMatches > 1) {
        ++output.type23PlacementDiagnostics.multipleIdentityMatches;
    }
    const std::size_t available = kCandidateCapacity - output.type23PlacementCandidates.size();
    const std::size_t retained = (std::min)(identityMatches, available);
    const bool candidatesComplete = retained == identityMatches;
    link.complete = candidatesComplete && identity_owner_inventory_complete(output);
    if (!candidatesComplete) {
        add_count(output.type23PlacementDiagnostics.droppedCandidates, identityMatches - retained);
        output.type23PlacementDiagnostics.complete = false;
    }

    const catalog::Object* const object = descriptor_object(output, descriptor);
    if (object == nullptr) {
        link.complete = false;
        output.type23PlacementDiagnostics.complete = false;
    }
    std::uint64_t active = 0;
    for (std::size_t offset = 0; offset < retained; ++offset) {
        const PlacementIndex& indexed = first[offset];
        catalog::Type23PlacementCandidate candidate{};
        candidate.linkRow = linkRow;
        candidate.placementRow = indexed.placementRow;
        if (indexed.placementRow >= output.containerPlacements.size() || object == nullptr) {
            link.complete = false;
            output.type23PlacementDiagnostics.complete = false;
        } else {
            candidate.applicableOwnerCount =
                applicable_owners(output,
                                  output.containerPlacements[indexed.placementRow],
                                  object->bubbleRow,
                                  candidate.ownerRow);
            active += candidate.applicableOwnerCount != 0 ? 1U : 0U;
        }
        output.type23PlacementCandidates.push_back(candidate);
    }
    // The sensor's own bubble selected nothing, so fall back to the rule the client uses: the
    // driven object only has to be one this scenario loads.
    if (active == 0 && object != nullptr) {
        std::uint64_t scenarioActive = 0;
        for (std::size_t row = link.firstCandidate; row < output.type23PlacementCandidates.size();
             ++row) {
            catalog::Type23PlacementCandidate& candidate = output.type23PlacementCandidates[row];
            if (candidate.placementRow >= output.containerPlacements.size()) {
                continue;
            }
            candidate.applicableOwnerCount = scenario_owners(
                output, output.containerPlacements[candidate.placementRow], candidate.ownerRow);
            scenarioActive += candidate.applicableOwnerCount != 0 ? 1U : 0U;
        }
        if (scenarioActive == 1) {
            ++output.type23PlacementDiagnostics.scenarioResolvedCandidates;
        }
        active = scenarioActive;
    }
    link.candidateCount = static_cast<std::uint32_t>(retained);
    link.activeCandidateCount = static_cast<std::uint32_t>((
        std::min)(active, static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
    if (active == 0) {
        ++output.type23PlacementDiagnostics.zeroActiveCandidates;
    } else if (active > 1) {
        ++output.type23PlacementDiagnostics.multipleActiveCandidates;
    }
    if (link.complete && active == 1) {
        for (std::size_t row = link.firstCandidate; row < output.type23PlacementCandidates.size();
             ++row) {
            if (output.type23PlacementCandidates[row].applicableOwnerCount != 0) {
                link.resolvedCandidate = static_cast<std::uint32_t>(row);
                link.join = catalog::ReferenceJoin::exact;
                break;
            }
        }
    } else if (link.complete && active > 1) {
        link.join = catalog::ReferenceJoin::ambiguous;
    }
    output.type23PlacementLinks.push_back(link);
    return true;
}

} // namespace

/** Builds a bounded equality graph from type-23 descriptor ids to authored placement ids. */
bool append_type23_placement_links(catalog::Snapshot& output,
                                   Type23PlacementCancelCheck cancel) noexcept {
    output.type23PlacementLinks.clear();
    output.type23PlacementCandidates.clear();
    output.type23PlacementDiagnostics = {};
    output.type23PlacementDiagnostics.complete = identity_owner_inventory_complete(output);
    for (catalog::Descriptor& descriptor : output.descriptors) {
        descriptor.placementLinkRow = catalog::kNoRow;
    }
    try {
        std::vector<PlacementIndex> index{};
        index.reserve(output.containerPlacements.size());
        for (std::size_t row = 0; row < output.containerPlacements.size(); ++row) {
            if (cancelled(cancel)) {
                return false;
            }
            const catalog::ContainerPlacement& placement = output.containerPlacements[row];
            if (placement.placementIdentifierRead) {
                index.push_back({placement.placementIdentifier, static_cast<std::uint32_t>(row)});
            }
        }
        std::sort(index.begin(),
                  index.end(),
                  [](const PlacementIndex& left, const PlacementIndex& right) noexcept {
                      return left.identifier != right.identifier
                                 ? left.identifier < right.identifier
                                 : left.placementRow < right.placementRow;
                  });
        output.type23PlacementLinks.reserve((std::min)(output.descriptors.size(), kLinkCapacity));
        output.type23PlacementCandidates.reserve(
            (std::min)(output.containerPlacements.size(), kCandidateCapacity));
        for (std::size_t row = 0; row < output.descriptors.size(); ++row) {
            if (cancelled(cancel)) {
                return false;
            }
            const catalog::Descriptor& descriptor = output.descriptors[row];
            if (descriptor.componentClass != tables::kType23ComponentClass
                || descriptor.slotRow >= output.slots.size()
                || output.slots[descriptor.slotRow].slotType != 23) {
                continue;
            }
            if (output.type23PlacementLinks.size() >= kLinkCapacity) {
                ++output.type23PlacementDiagnostics.droppedLinks;
                output.type23PlacementDiagnostics.complete = false;
                continue;
            }
            if (!append_link(output, static_cast<std::uint32_t>(row), index)) {
                return false;
            }
        }
        report_unresolved_links(output);
        return !cancelled(cancel);
    } catch (...) {
        output.type23PlacementDiagnostics.complete = false;
        return false;
    }
}

} // namespace sunrise::client::content::activity::scriptables::internal
