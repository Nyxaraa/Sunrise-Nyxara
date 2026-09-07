/** World reward queue: rewards earned outside a request, held until a peer can present one. */

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../core/logging/log.h"
#include "internal.h"

namespace sunrise::server::bap {
namespace {

/** Rewards earned in world, oldest first, waiting for a Family-4 peer to present them. */
std::array<WorldRewardRequest, kWorldRewardQueueCapacity> g_worldRewards{};
std::size_t g_worldRewardHead{};
std::size_t g_worldRewardCount{};

/** Drops the oldest queued reward. */
void pop_world_reward() noexcept {
    g_worldRewards[g_worldRewardHead] = {};
    g_worldRewardHead = (g_worldRewardHead + 1) % g_worldRewards.size();
    --g_worldRewardCount;
}

/** Commits one reward straight into State, with no presentation. @return True when it lands. */
[[nodiscard]] bool commit_world_reward(const WorldRewardRequest& request) noexcept {
    if (request.kind == WorldRewardKind::item) {
        state::PendingItemAcquisition acquisition{};
        return state::prepare_item_acquisition_for_item(request.itemDefinitionIndex, acquisition)
               && state::commit_item_acquisition(acquisition);
    }
    state::PendingProfileItemAcquisition acquisition{};
    return state::prepare_profile_item_acquisition_for_item(
               request.itemDefinitionIndex, request.quantity, acquisition)
           && state::commit_profile_item_acquisition(acquisition);
}

/**
 * Queues one reward for presentation, or commits it now when no peer can present it.
 * @param request Reward to grant.
 * @return True when the reward is queued or committed.
 */
[[nodiscard]] bool enqueue_world_reward(WorldRewardRequest request) noexcept {
    if (!has_active_family4_peer()) {
        while (g_worldRewardCount != 0) {
            if (!commit_world_reward(g_worldRewards[g_worldRewardHead])) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=world_reward stage=direct result=drop");
            }
            pop_world_reward();
        }
        return commit_world_reward(request);
    }
    if (g_worldRewardCount == g_worldRewards.size()) {
        const bool committed = commit_world_reward(g_worldRewards[g_worldRewardHead]);
        if (committed) {
            arm_account_resync_everywhere();
        }
        pop_world_reward();
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         committed ? "ev=world_reward stage=queue_full result=direct"
                                   : "ev=world_reward stage=queue_full result=drop");
    }
    g_worldRewards[(g_worldRewardHead + g_worldRewardCount) % g_worldRewards.size()] = request;
    ++g_worldRewardCount;
    return true;
}

} // namespace

bool arm_world_item_acquisition(std::uint16_t itemDefinitionIndex) noexcept {
    return enqueue_world_reward({1, itemDefinitionIndex, WorldRewardKind::item});
}

bool arm_world_profile_item_acquisition(std::uint16_t itemDefinitionIndex,
                                        std::int32_t quantity) noexcept {
    if (quantity <= 0) {
        return false;
    }
    return enqueue_world_reward({quantity, itemDefinitionIndex, WorldRewardKind::profileItem});
}

bool current_world_reward(WorldRewardRequest& request) noexcept {
    if (g_worldRewardCount == 0) {
        request = {};
        return false;
    }
    request = g_worldRewards[g_worldRewardHead];
    return true;
}

void complete_world_reward() noexcept {
    if (g_worldRewardCount == 0) {
        return;
    }
    pop_world_reward();
}

/** Commits the queued reward with no flyout once its presentation cannot be built. */
void settle_world_reward() noexcept {
    if (g_worldRewardCount == 0) {
        return;
    }
    const bool committed = commit_world_reward(g_worldRewards[g_worldRewardHead]);
    pop_world_reward();
    if (committed) {
        arm_account_resync_everywhere();
    }
    core::log::write(core::log::Channel::server,
                     core::log::Level::warn,
                     committed ? "ev=world_reward stage=settle result=direct"
                               : "ev=world_reward stage=settle result=drop");
}

/** Commits every queued reward with no presentation and empties the queue. */
void drain_world_rewards() noexcept {
    while (g_worldRewardCount != 0) {
        if (!commit_world_reward(g_worldRewards[g_worldRewardHead])) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             "ev=world_reward stage=shutdown result=drop");
        }
        pop_world_reward();
    }
    g_worldRewards = {};
    g_worldRewardHead = 0;
}

} // namespace sunrise::server::bap
