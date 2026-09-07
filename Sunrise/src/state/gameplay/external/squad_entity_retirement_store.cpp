#include <algorithm>

#include "squad_entity_retirement.h"

namespace sunrise::state::gameplay::squad_entity_retirement {
namespace {
bool bit(const Mask& mask, std::size_t slot) {
    return (std::to_integer<unsigned>(mask[slot / 8]) & (1U << (slot % 8))) != 0;
}
void set(Mask& mask, std::size_t slot) {
    mask[slot / 8] |= std::byte(1U << (slot % 8));
}
bool any(const Mask& mask) {
    return std::any_of(mask.begin(), mask.end(), [](auto value) { return value != std::byte{}; });
}
bool same(const identities::Identity& a, const identities::Identity& b) {
    return a.known && b.known && a.present && b.present && !a.conflicted && !b.conflicted
           && a.token == b.token && a.allocationSequence == b.allocationSequence
           && a.allocationEpoch == b.allocationEpoch && a.allocationDomain == b.allocationDomain
           && a.hasAllocationEpoch == b.hasAllocationEpoch && a.type == b.type
           && a.metadata == b.metadata && a.actorSource == b.actorSource && a.cell == b.cell
           && a.anchorKnown && b.anchorKnown && a.anchorPresent == b.anchorPresent
           && (!a.anchorPresent || a.anchor == b.anchor);
}
bool actor(const identities::Identity& row, const Eligibility& eligibility) {
    return row.known && row.present && !row.conflicted && row.type == 0 && row.metadata.hasRsat
           && row.metadata.rsatTag == eligibility.rsatTag && !row.metadata.hasPlayerBroadcast
           && row.actorSource.known && row.actorSource.present && row.actorSource.type == 1
           && row.actorSource.key == eligibility.squad.key
           && row.actorSource.type == eligibility.squad.type
           && row.actorSource.index == eligibility.squad.index;
}
/** Only an anchored weapon may use its parent tree when its own source was not reported. */
bool weapon(const identities::Identity& row, const Eligibility& eligibility) {
    /** Native object-type 14 is the weapon class. */
    constexpr std::uint8_t kWeaponObjectType = 14;
    if (row.type != 0 || !row.metadata.hasRsat || !row.metadata.hasObjectType
        || row.metadata.objectType != kWeaponObjectType || row.metadata.hasPlayerBroadcast
        || !row.anchorKnown || !row.anchorPresent) {
        return false;
    }
    return !row.actorSource.known || !row.actorSource.present
           || (row.actorSource.type == eligibility.squad.type
               && row.actorSource.key == eligibility.squad.key
               && row.actorSource.index == eligibility.squad.index);
}
/** Every live anchor must resolve before a root's complete child set is trustworthy. */
bool hierarchy(std::span<const identities::Identity> rows) {
    if (rows.size() != identities::kSlotCapacity) {
        return false;
    }
    for (std::size_t slot = 0; slot < rows.size(); ++slot) {
        const auto& row = rows[slot];
        if (!row.present) {
            continue;
        }
        if (!row.known || row.conflicted || !row.anchorKnown || row.token.slot != slot) {
            return false;
        }
        if (row.anchorPresent
            && (row.anchor.slot >= rows.size() || !rows[row.anchor.slot].present
                || rows[row.anchor.slot].token != row.anchor)) {
            return false;
        }
    }
    return true;
}
/** Closure traversal stops at unknown roles rather than retireing an unrelated descendant. */
bool closure(identities::Token root,
             const Eligibility& eligibility,
             const Mask& released,
             std::span<const identities::Identity> rows,
             Mask& output) {
    output = {};
    if (root.slot >= rows.size() || rows[root.slot].token != root
        || !actor(rows[root.slot], eligibility) || rows[root.slot].anchorPresent
        || !bit(released, root.slot)) {
        return false;
    }
    set(output, root.slot);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& row : rows) {
            if (!row.present || !row.anchorPresent || !bit(output, row.anchor.slot)
                || bit(output, row.token.slot)) {
                continue;
            }
            const bool squad =
                row.type == 1 && row.metadata.hasSquad && row.metadata.squad == eligibility.squad;
            if (!bit(released, row.token.slot)
                || (!actor(row, eligibility) && !weapon(row, eligibility) && !squad)) {
                return false;
            }
            set(output, row.token.slot);
            changed = true;
        }
    }
    return true;
}
} // namespace
/** Captures only complete, opted-in actor trees from the authenticated release report. */
bool Store::capture(const identities::Source& source,
                    std::uint8_t bubble,
                    const Mask& released,
                    std::span<const identities::Identity> rows,
                    std::span<const Eligibility> eligibility,
                    const CellBubbles& cells) noexcept {
    try {
        if (source.activitySessionId == 0 || source.activityClientGeneration == 0 || bubble >= 64
            || !hierarchy(rows)) {
            return false;
        }
        for (auto& previous : releases_) {
            if (previous.source != source || previous.bubble == bubble) {
                continue;
            }
            const auto count = previous.groups.size();
            std::erase_if(previous.groups, [&](const Release::Group& group) {
                for (std::size_t i = 0; i < released.size(); ++i) {
                    if ((released[i] & group.entities[i]) != std::byte{}) {
                        return true;
                    }
                }
                return false;
            });
            if (previous.groups.size() != count) {
                previous.revision = ++revision_;
            }
        }
        std::erase_if(releases_, [](const Release& previous) { return previous.groups.empty(); });
        Release next;
        next.source = source;
        next.bubble = bubble;
        next.revision = ++revision_;
        for (const auto& row : rows) {
            if (!row.present || row.anchorPresent || !bit(released, row.token.slot)
                || row.cell >= cells.size() || cells[row.cell] != bubble) {
                continue;
            }
            const Eligibility* selected = nullptr;
            for (const auto& candidate : eligibility) {
                if (candidate.enabled && candidate.bubble == bubble && actor(row, candidate)) {
                    if (selected) {
                        selected = nullptr;
                        break;
                    }
                    selected = &candidate;
                }
            }
            if (!selected || std::count_if(rows.begin(), rows.end(), [&](const auto& candidate) {
                                 return !candidate.anchorPresent && actor(candidate, *selected);
                             }) != 1) {
                continue;
            }
            Release::Group group;
            group.root = row.token;
            group.eligibility = *selected;
            if (!closure(row.token, *selected, released, rows, group.entities)) {
                continue;
            }
            bool valid = true;
            for (std::size_t slot = 0; slot < rows.size(); ++slot) {
                if (bit(group.entities, slot)) {
                    const auto& member = rows[slot];
                    if (member.cell >= cells.size() || cells[member.cell] != bubble) {
                        valid = false;
                        break;
                    }
                    group.captured.push_back(member);
                }
            }
            if (valid) {
                next.groups.push_back(std::move(group));
            }
        }
        for (const auto& old : releases_) {
            if (old.source != source || old.bubble != bubble) {
                continue;
            }
            for (const auto& group : old.groups) {
                bool overlaps = false;
                for (std::size_t i = 0; i < released.size(); ++i) {
                    if ((released[i] & group.entities[i]) != std::byte{}) {
                        overlaps = true;
                        break;
                    }
                }
                if (overlaps) {
                    continue;
                }
                Mask current{};
                if (!closure(group.root, group.eligibility, group.entities, rows, current)
                    || current != group.entities) {
                    continue;
                }
                if (std::all_of(
                        group.captured.begin(), group.captured.end(), [&](const auto& before) {
                            return same(before, rows[before.token.slot]);
                        })) {
                    next.groups.push_back(group);
                }
            }
        }
        std::erase_if(releases_, [&](const Release& old) {
            return old.source.activitySessionId == source.activitySessionId
                   && old.source.activityClientGeneration == source.activityClientGeneration
                   && (old.source != source || old.bubble == bubble);
        });
        /** Pending releases are bounded by the native source and bubble domains. */
        constexpr std::size_t kMaximumReleases = identities::kSourceCapacity * 64;
        if (next.groups.empty() || releases_.size() >= kMaximumReleases) {
            return false;
        }
        releases_.push_back(std::move(next));
        return true;
    } catch (...) {
        return false;
    }
}
/** Preparation compares the release's lifetime and full tree against a fresh atomic snapshot. */
bool Store::prepare(const identities::Source& source,
                    std::uint8_t bubble,
                    std::span<const identities::Identity> rows,
                    RetirementPlan& output) const noexcept {
    output = {};
    if (!hierarchy(rows)) {
        return false;
    }
    for (const auto& release : releases_) {
        if (release.source != source || release.bubble != bubble) {
            continue;
        }
        for (const auto& group : release.groups) {
            if (std::count_if(rows.begin(),
                              rows.end(),
                              [&](const auto& row) {
                                  return !row.anchorPresent && actor(row, group.eligibility);
                              })
                != 1) {
                continue;
            }
            Mask current{};
            if (!closure(group.root, group.eligibility, group.entities, rows, current)
                || current != group.entities) {
                continue;
            }
            bool valid = true;
            for (const auto& before : group.captured) {
                if (!same(before, rows[before.token.slot])) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                for (std::size_t index = 0; index < output.entities.size(); ++index) {
                    output.entities[index] |= group.entities[index];
                }
            }
        }
        if (!any(output.entities)) {
            return false;
        }
        for (std::size_t slot = 0; slot < rows.size(); ++slot) {
            if ((std::to_integer<unsigned>(output.entities[slot / 8]) & (1U << (slot % 8))) != 0) {
                if (output.lifetimeCount == output.lifetimes.size()) {
                    output = {};
                    return false;
                }
                output.lifetimes[output.lifetimeCount++] = {rows[slot].token,
                                                            rows[slot].allocationSequence,
                                                            rows[slot].allocationEpoch,
                                                            rows[slot].allocationDomain};
            }
        }
        output.source = source;
        output.bubble = bubble;
        output.revision = release.revision;
        output.pending = true;
        return true;
    }
    return false;
}
/** A discarded or obsolete plan consumes no released entity. */
bool Store::commit(const RetirementPlan& plan) noexcept {
    if (!plan.pending || !any(plan.entities) || plan.lifetimeCount == 0
        || plan.lifetimeCount > plan.lifetimes.size()) {
        return false;
    }
    for (auto& release : releases_) {
        if (release.source == plan.source && release.bubble == plan.bubble
            && release.revision == plan.revision) {
            Mask covered{};
            for (const auto& group : release.groups) {
                bool complete = true;
                for (std::size_t i = 0; i < plan.entities.size(); ++i) {
                    if ((group.entities[i] & plan.entities[i]) != group.entities[i]) {
                        complete = false;
                        break;
                    }
                }
                if (complete) {
                    for (std::size_t i = 0; i < covered.size(); ++i) {
                        covered[i] |= group.entities[i];
                    }
                }
            }
            if (covered != plan.entities) {
                return false;
            }
            Mask exact{};
            for (const auto& lifetime : plan.retired_lifetimes()) {
                if (lifetime.token.slot >= identities::kSlotCapacity) {
                    return false;
                }
                const auto byte = lifetime.token.slot / 8;
                const auto bit = static_cast<std::byte>(1U << (lifetime.token.slot % 8));
                if ((plan.entities[byte] & bit) == std::byte{}
                    || (exact[byte] & bit) != std::byte{}) {
                    return false;
                }
                bool found = false;
                for (const auto& group : release.groups) {
                    for (const auto& row : group.captured) {
                        if (row.token == lifetime.token
                            && row.allocationSequence == lifetime.allocationSequence
                            && row.allocationEpoch == lifetime.allocationEpoch
                            && row.allocationDomain == lifetime.allocationDomain) {
                            found = true;
                        }
                    }
                }
                if (!found) {
                    return false;
                }
                exact[byte] |= bit;
            }
            if (exact != plan.entities) {
                return false;
            }
            std::erase_if(release.groups, [&](const Release::Group& group) {
                for (std::size_t i = 0; i < plan.entities.size(); ++i) {
                    if ((group.entities[i] & plan.entities[i]) != group.entities[i]) {
                        return false;
                    }
                }
                return true;
            });
            release.revision = ++revision_;
            return true;
        }
    }
    return false;
}
/** Returning any member invalidates its entire captured tree. */
void Store::returned_slots(std::uint64_t session,
                           std::uint64_t generation,
                           const Mask& mask) noexcept {
    for (auto& release : releases_) {
        if (release.source.activitySessionId == session
            && release.source.activityClientGeneration == generation) {
            std::erase_if(release.groups, [&](const Release::Group& group) {
                for (std::size_t i = 0; i < mask.size(); ++i) {
                    if ((group.entities[i] & mask[i]) != std::byte{}) {
                        return true;
                    }
                }
                return false;
            });
            release.revision = ++revision_;
        }
    }
}
/** Changing one authored target cannot consume another squad's pending release. */
void Store::invalidate_target(std::uint64_t session,
                              std::uint64_t generation,
                              identities::SquadReference target) noexcept {
    for (auto& release : releases_) {
        if (release.source.activitySessionId == session
            && release.source.activityClientGeneration == generation) {
            const auto old = release.groups.size();
            std::erase_if(release.groups, [&](const Release::Group& group) {
                return group.eligibility.squad == target;
            });
            if (release.groups.size() != old) {
                release.revision = ++revision_;
            }
        }
    }
}
void Store::reset() noexcept {
    releases_.clear();
    ++revision_;
}
} // namespace sunrise::state::gameplay::squad_entity_retirement
