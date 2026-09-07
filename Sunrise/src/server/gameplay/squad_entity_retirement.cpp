#include "squad_entity_retirement.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>

#include "../../core/logging/log.h"
#include "../../state/gameplay/external/entity_object_types.h"
#include "../../state/gameplay/external/entity_position_profiles.h"
#include "../activity/host_runtime.h"
#include "entity_identities.h"
#include "peer/peer_transport.h"

namespace sunrise::server::gameplay::squad_entity_retirement {
namespace {
namespace policy = state::gameplay::squad_entity_retirement;
namespace identities = state::gameplay::entity_identity;
SRWLOCK g_lock{SRWLOCK_INIT};
policy::Store g_store;
struct Target final {
    std::uint64_t session{}, revision{}, generation{};
    policy::Eligibility eligibility{};
};
std::vector<Target> g_targets;
/** Exactly one admitted current view must own the ActivityClient generation. */
bool snapshot(const state::activity::SessionBinding& binding,
              std::uint64_t generation,
              identities::Source& source,
              std::vector<identities::Identity>& rows) noexcept {
    source = {};
    rows.clear();
    if (!generation || !state::activity::binding_matches(binding)) {
        return false;
    }
    std::array<identities::Source, identities::kSourceCapacity> sources{};
    const auto count =
        entity_identities::sources(binding.sessionId, binding.createdRevision, sources);
    if (count > sources.size()) {
        return false;
    }
    std::size_t matches = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (sources[i].activityClientGeneration == generation) {
            source = sources[i];
            ++matches;
        }
    }
    return matches == 1
           && entity_identities::snapshot_source(source, rows) == identities::Result::unchanged
           && state::gameplay::entity_object_types::enrich_snapshot(rows);
}
/** Reports bounded release evidence without granting unknown identities any authority. */
void report_released(const policy::Mask& mask,
                     std::span<const identities::Identity> rows) noexcept {
    if (!core::log::accepts(core::log::Channel::server, core::log::Level::debug)) {
        return;
    }
    // Bounds one release report; the mask still carries the whole selection.
    constexpr std::size_t kMaximumReportedSlots = 64;
    std::size_t shown = 0;
    for (std::size_t slot = 0; slot < rows.size() && shown < kMaximumReportedSlots; ++slot) {
        if ((std::to_integer<unsigned>(mask[slot / 8]) & (1U << (slot % 8))) == 0) {
            continue;
        }
        ++shown;
        const auto& row = rows[slot];
        const auto& actor = row.actorSource;
        std::array<char, core::log::kLineCapacity> line{};
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "squad_entity_retirement stage=released slot=%zu known=%u present=%u conflict=%u "
            "inc=%u alloc=%u type=%u cell=%u parent_known=%u parent=%d rsat=0x%08X "
            "object_type=%d source_known=%u source_present=%u source_key=0x%08X "
            "source_type=%u source_index=%u",
            slot,
            unsigned(row.known),
            unsigned(row.present),
            unsigned(row.conflicted),
            unsigned(row.token.incarnation),
            unsigned(row.allocationSequence),
            unsigned(row.type),
            unsigned(row.cell),
            unsigned(row.anchorKnown),
            row.anchorPresent ? int(row.anchor.slot) : -1,
            row.metadata.rsatTag,
            row.metadata.hasObjectType ? int(row.metadata.objectType) : -1,
            unsigned(actor.known),
            unsigned(actor.present),
            actor.key,
            unsigned(actor.type),
            unsigned(actor.index));
        if (count > 0) {
            core::log::write(
                core::log::Channel::server,
                core::log::Level::debug,
                {line.data(), (std::min)(static_cast<std::size_t>(count), line.size() - 1)});
        }
    }
}

/** Missing parents must remain visible when they block a complete retire tree. */
void report_hierarchy_gaps(std::span<const identities::Identity> rows) noexcept {
    if (!core::log::accepts(core::log::Channel::server, core::log::Level::debug)) {
        return;
    }
    // Bounds one hierarchy-gap report.
    constexpr std::size_t kMaximumReportedGaps = 64;
    std::size_t shown = 0;
    for (std::size_t slot = 0; slot < rows.size() && shown < kMaximumReportedGaps; ++slot) {
        const auto& row = rows[slot];
        if (!row.present) {
            continue;
        }
        const char* reason = nullptr;
        if (!row.known || row.conflicted || !row.anchorKnown || row.token.slot != slot) {
            reason = "identity";
        } else if (row.anchorPresent
                   && (row.anchor.slot >= rows.size() || !rows[row.anchor.slot].present)) {
            reason = "missing_parent";
        } else if (row.anchorPresent && rows[row.anchor.slot].token != row.anchor) {
            reason = "parent_lifetime";
        }
        if (reason == nullptr) {
            continue;
        }
        ++shown;
        std::array<char, core::log::kLineCapacity> line{};
        const int count = std::snprintf(line.data(),
                                        line.size(),
                                        "squad_entity_retirement stage=hierarchy_gap slot=%zu "
                                        "reason=%s parent=%d parent_inc=%u",
                                        slot,
                                        reason,
                                        row.anchorPresent ? int(row.anchor.slot) : -1,
                                        unsigned(row.anchor.incarnation));
        if (count > 0) {
            core::log::write(
                core::log::Channel::server,
                core::log::Level::debug,
                {line.data(), (std::min)(static_cast<std::size_t>(count), line.size() - 1)});
        }
    }
}

/** Logs the complete selected count and a bounded prefix of slot indices. */
void report(const char* stage,
            bool result,
            std::uint8_t bubble,
            const policy::Mask* mask = nullptr) {
    std::array<char, core::log::kLineCapacity> line{};
    std::size_t selected = 0;
    if (mask) {
        for (auto byte : *mask) {
            for (unsigned bit = 0; bit < 8; ++bit) {
                selected += (std::to_integer<unsigned>(byte) >> bit) & 1U;
            }
        }
    }
    const int prefix =
        std::snprintf(line.data(),
                      line.size(),
                      "squad_entity_retirement stage=%s bubble=%u accepted=%u selected=%zu slots=",
                      stage,
                      unsigned(bubble),
                      result ? 1U : 0U,
                      selected);
    if (prefix <= 0) {
        return;
    }
    std::size_t length = static_cast<std::size_t>(prefix), shown = 0;
    if (mask) {
        for (std::size_t slot = 0; slot < identities::kSlotCapacity && shown < 32; ++slot) {
            if ((std::to_integer<unsigned>((*mask)[slot / 8]) & (1U << (slot % 8))) == 0) {
                continue;
            }
            const int count = std::snprintf(
                line.data() + length, line.size() - length, "%s%zu", shown ? "," : "", slot);
            if (count <= 0 || static_cast<std::size_t>(count) >= line.size() - length) {
                break;
            }
            length += static_cast<std::size_t>(count);
            ++shown;
        }
    }
    if (shown == 0) {
        line[length++] = '-';
    }
    core::log::write(core::log::Channel::server, core::log::Level::info, {line.data(), length});
}
} // namespace
/** Delivered squad choices replace eligibility for their exact authored target. */
void record_delivered_target(const state::activity::SessionBinding& binding,
                             std::uint64_t generation,
                             const activity::host::PendingScriptableOverride& pending) noexcept {
    if (!generation || pending.target.slotType != 1
        || pending.expectedActivityClientGeneration != generation) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    try {
        const auto& selected = pending.squadRetirement;
        const auto prior = std::find_if(g_targets.begin(), g_targets.end(), [&](const Target& row) {
            return row.session == binding.sessionId && row.revision == binding.createdRevision
                   && row.generation == generation
                   && row.eligibility.squad.key == pending.target.registryKey
                   && row.eligibility.squad.index == pending.target.slotIndex;
        });
        const bool enabled = pending.kind == activity::host::ScriptableOverrideKind::squad
                             && selected.enabled && selected.squad.key == pending.target.registryKey
                             && selected.squad.index == pending.target.slotIndex
                             && selected.squad.type == 1;
        const bool unchanged =
            prior != g_targets.end() && enabled && prior->eligibility == selected;
        if (!unchanged) {
            g_store.invalidate_target(
                binding.sessionId,
                generation,
                {pending.target.registryKey, pending.target.slotIndex, pending.target.slotType});
            std::erase_if(g_targets, [&](const Target& row) {
                return row.session == binding.sessionId
                       && (row.revision != binding.createdRevision || row.generation != generation
                           || (row.eligibility.squad.key == pending.target.registryKey
                               && row.eligibility.squad.index == pending.target.slotIndex));
            });
            /** An active source cannot own more actor targets than native entity slots. */
            constexpr std::size_t kMaximumTargets =
                identities::kSourceCapacity * identities::kSlotCapacity;
            if (enabled && g_targets.size() < kMaximumTargets) {
                g_targets.push_back(
                    {binding.sessionId, binding.createdRevision, generation, selected});
            }
        }
    } catch (...) {
        g_store.invalidate_target(
            binding.sessionId,
            generation,
            {pending.target.registryKey, pending.target.slotIndex, pending.target.slotType});
    }
    ReleaseSRWLockExclusive(&g_lock);
}
/** Only package-mapped, positively authored trees enter a captured release. */
void observe_abdication(const state::activity::SessionBinding& binding,
                        std::uint64_t generation,
                        std::uint8_t bubble,
                        const state::activity::bubble_authority::EntitySlotMask& mask) noexcept {
    identities::Source source{};
    std::vector<identities::Identity> rows;
    if (!snapshot(binding, generation, source, rows)) {
        report("capture_source", false, bubble);
        return;
    }
    const auto& destination = binding.destination;
    report_released(mask, rows);
    report_hierarchy_gaps(rows);
    if (destination.packageNameLength == 0
        || destination.packageNameLength > destination.packageName.size()) {
        return;
    }
    const std::string_view name(reinterpret_cast<const char*>(destination.packageName.data()),
                                destination.packageNameLength);
    policy::CellBubbles cells{};
    cells.fill(-1);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        std::uint8_t owner{};
        if (state::gameplay::entity_position_profiles::lookup_bubble(
                name, static_cast<std::uint16_t>(i), owner)) {
            cells[i] = owner;
        }
    }
    AcquireSRWLockExclusive(&g_lock);
    bool accepted = false;
    try {
        std::vector<policy::Eligibility> eligible;
        for (const auto& row : g_targets) {
            if (row.session == binding.sessionId && row.revision == binding.createdRevision
                && row.generation == generation) {
                eligible.push_back(row.eligibility);
            }
        }
        accepted = g_store.capture(source, bubble, mask, rows, eligible, cells);
    } catch (...) {}
    ReleaseSRWLockExclusive(&g_lock);
    report("capture", accepted, bubble);
}
void returned_slots(const state::activity::SessionBinding& binding,
                    std::uint64_t generation,
                    const state::activity::bubble_authority::EntitySlotMask& mask) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_store.returned_slots(binding.sessionId, generation, mask);
    ReleaseSRWLockExclusive(&g_lock);
}
/** A fresh atomic identity snapshot must still match each captured tree. */
bool prepare_retirement(const state::activity::SessionBinding& binding,
                        std::uint64_t generation,
                        std::uint8_t bubble,
                        RetirementPlan& output) noexcept {
    output = {};
    identities::Source source{};
    std::vector<identities::Identity> rows;
    if (!snapshot(binding, generation, source, rows)) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    const bool ready = g_store.prepare(source, bubble, rows, output);
    ReleaseSRWLockShared(&g_lock);
    report("prepare", ready, bubble, &output.entities);
    return ready;
}
/** The exact identity view remains pinned until its transport response has been copied. */
bool begin_retirement_publication(const state::activity::SessionBinding& binding,
                                  std::uint64_t generation,
                                  const RetirementPlan& plan,
                                  entity_identities::PublicationLease& lease) noexcept {
    if (lease.held()) {
        return false;
    }
    if (!plan.pending || plan.source.activitySessionId != binding.sessionId
        || plan.source.activityRevision != binding.createdRevision
        || plan.source.activityClientGeneration != generation
        || !state::activity::binding_matches(binding)) {
        return false;
    }
    std::vector<identities::Identity> rows;
    if (entity_identities::begin_publication(plan.source, rows, lease)
        != identities::Result::unchanged) {
        return false;
    }
    if (!state::gameplay::entity_object_types::enrich_snapshot(rows)) {
        lease.release();
        return false;
    }
    RetirementPlan current{};
    AcquireSRWLockShared(&g_lock);
    const bool valid = g_store.prepare(plan.source, plan.bubble, rows, current)
                       && current.source == plan.source && current.entities == plan.entities
                       && current.lifetimes == plan.lifetimes
                       && current.lifetimeCount == plan.lifetimeCount
                       && current.revision == plan.revision && current.bubble == plan.bubble;
    ReleaseSRWLockShared(&g_lock);
    if (!valid) {
        lease.release();
    }
    return valid;
}
/** A stale prepared retirement cannot enter a later transport publication. */
bool validate_retirement(const state::activity::SessionBinding& binding,
                         std::uint64_t generation,
                         const RetirementPlan& plan) noexcept {
    RetirementPlan current{};
    return plan.pending && prepare_retirement(binding, generation, plan.bubble, current)
           && current.source == plan.source && current.entities == plan.entities
           && current.lifetimes == plan.lifetimes && current.lifetimeCount == plan.lifetimeCount
           && current.revision == plan.revision && current.bubble == plan.bubble;
}
/** Retires exact lifetimes only after their carrying publication and identity lease have ended. */
void commit_retirement(const RetirementPlan& plan) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const bool committed = g_store.commit(plan);
    if (committed) {
        g_store.returned_slots(
            plan.source.activitySessionId, plan.source.activityClientGeneration, plan.entities);
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (committed) {
        static_cast<void>(entity_identities::retire(plan.source, plan.retired_lifetimes()));
        static_cast<void>(peer::retire_entity_baselines(plan.source, plan.retired_lifetimes()));
    }
    report("commit", committed, plan.bubble, &plan.entities);
}
void reset() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_store.reset();
    g_targets.clear();
    ReleaseSRWLockExclusive(&g_lock);
}
} // namespace sunrise::server::gameplay::squad_entity_retirement
