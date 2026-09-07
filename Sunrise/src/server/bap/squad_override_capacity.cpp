#include "squad_override_capacity.h"

#include "../../middleware/bap/activity_message/sensor_auth_update.h"
#include "../../middleware/content/packages/tables/region_reader.h"
#include "../../state/build_data/runtime.h"
#include "internal.h"

namespace sunrise::server::bap::squad_override_capacity {
namespace {

namespace layouts = state::build_data::scenarios;
namespace roster_message = middleware::bap::activity_message::sensor_auth_update;
namespace tables = middleware::content::packages::tables;

/** Active ClientRef manager usage for the ungated top-level list plus one bubble list. */
struct ClientBudget final {
    std::size_t groups{};
    std::size_t records{};
};

/** Adds one active group without crossing either fixed ClientRef manager pool. */
[[nodiscard]] bool add_group(std::size_t slots, ClientBudget& budget) noexcept {
    if (budget.groups >= roster_message::kClientGroupCapacity
        || budget.records > roster_message::kClientRecordCapacity
        || slots > roster_message::kClientRecordCapacity - budget.records) {
        return false;
    }
    ++budget.groups;
    budget.records += slots;
    return true;
}

/** Loads every canonical group and counts only top-level plus one requested bubble. */
[[nodiscard]] bool add_canonical(const layouts::Definition& layout,
                                 std::size_t bubble,
                                 ClientBudget& budget) noexcept {
    const std::size_t topCount = layout.rosterGroupCount;
    const std::size_t bubbleCount = layout.bubbleGroupCount;
    if (bubble >= layouts::kBubbleCapacity || topCount > layout.rosterGroups.size()
        || bubbleCount > layout.bubbleGroups.size()
        || bubbleCount > layout.bubbleGroupMasks.size()) {
        return false;
    }
    for (std::size_t index = 0; index < topCount + bubbleCount; ++index) {
        const std::uint16_t tableIndex =
            index < topCount ? layout.rosterGroups[index] : layout.bubbleGroups[index - topCount];
        layouts::RosterGroup group{};
        if (!state::build_data::find_roster_group(tableIndex, group)
            || !layouts::valid_roster_group(group)) {
            return false;
        }
        const bool active =
            index < topCount
            || (layout.bubbleGroupMasks[index - topCount] & (std::uint64_t{1} << bubble)) != 0;
        if (active && !add_group(group.slotCount, budget)) {
            return false;
        }
    }
    return true;
}

} // namespace

/** Checks the active ClientRef manager budget for one production squad admission. */
bool available(const layouts::Definition& layout,
               const SquadOverrideLease* lease,
               const layouts::RosterGroup* pendingGroup,
               bool stateLocalTarget,
               std::int32_t targetRegion,
               std::int32_t selectedRegion) noexcept {
    const std::int32_t budgetRegion = stateLocalTarget ? targetRegion : selectedRegion;
    if (budgetRegion < 0) {
        return false;
    }
    const std::size_t bubble =
        static_cast<std::size_t>(budgetRegion) / tables::kSliceSetIndexFactor;
    ClientBudget budget{};
    if (!add_canonical(layout, bubble, budget)) {
        return false;
    }
    if (lease != nullptr) {
        if (!lease->active || lease->groupCount == 0 || lease->groupCount > lease->groups.size()) {
            return false;
        }
        for (std::size_t index = 0; index < lease->groupCount; ++index) {
            const RetainedSquadGroup& retained = lease->groups[index];
            if (!retained.scopeTarget.stateLocalRoster) {
                continue;
            }
            if (retained.region < 0
                || !layouts::valid_roster_group(retained.stateLocalRosterGroup)) {
                return false;
            }
            const std::size_t retainedBubble =
                static_cast<std::size_t>(retained.region) / tables::kSliceSetIndexFactor;
            if (retainedBubble == bubble
                && !add_group(retained.stateLocalRosterGroup.slotCount, budget)) {
                return false;
            }
        }
    }
    return pendingGroup == nullptr
           || (stateLocalTarget && layouts::valid_roster_group(*pendingGroup)
               && add_group(pendingGroup->slotCount, budget));
}

} // namespace sunrise::server::bap::squad_override_capacity
