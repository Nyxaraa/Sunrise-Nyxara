#include <limits>

#include "../../runtime.h"
#include "reward_catalog.h"

namespace sunrise::state::build_data {
namespace {

struct ResolveContext {
    std::array<records::rewards::ResolvedReward, records::rewards::kRewardPerRecordCapacity>*
        rewards{};
    std::size_t count{};
    bool valid{true};
};

bool resolve_installed(void* context, const records::rewards::RewardRow& row) noexcept {
    auto& resolution = *static_cast<ResolveContext*>(context);
    items::Definition item{};
    if (!find_item_definition_hash(row.itemHash, item)) {
        return true;
    }
    if (row.quantity == 0
        || row.quantity > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())
        || resolution.count >= resolution.rewards->size()) {
        resolution.valid = false;
        return false;
    }
    (*resolution.rewards)[resolution.count++] = {item.definitionIndex,
                                                 static_cast<std::int32_t>(row.quantity)};
    return true;
}

} // namespace

bool find_generated_record_rewards(std::uint32_t recordHash,
                                   std::array<records::rewards::ResolvedReward,
                                              records::rewards::kRewardPerRecordCapacity>& rewards,
                                   std::size_t& rewardCount) noexcept {
    rewards = {};
    ResolveContext resolution{&rewards};
    records::rewards::visit_for_record(recordHash, resolve_installed, &resolution);
    rewardCount = resolution.count;
    return resolution.valid;
}

} // namespace sunrise::state::build_data
