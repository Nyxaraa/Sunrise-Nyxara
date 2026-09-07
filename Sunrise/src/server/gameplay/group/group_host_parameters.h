#pragma once

#include <cstdint>

#include "../../../middleware/gameplay/group/parameter_messages.h"

namespace sunrise::server::gameplay::group {

/**
 * Sends one parameter update on the reliable channel of the session it names.
 * @param update Body to publish. Its `sessionId` picks the channel.
 * @return True when the update was queued.
 */
[[nodiscard]] bool
send_parameter_update(const middleware::gameplay::group::ParameterUpdate& update) noexcept;

/**
 * Publishes the `activity-host` parameter for one admitted peer.
 * @param sessionId Group session whose reliable channel carries it.
 * @return True when the update was queued.
 */
[[nodiscard]] bool publish_activity_host(std::uint64_t sessionId) noexcept;

/**
 * Answers one parameter request with the parameters this host can encode.
 * @param sessionId Session the request named, which is also the link it goes back on.
 * @param requested Requested parameter mask, already reduced to its meaningful bits.
 * @param playerCount Players the session holds now, which sets the free join slots it advertises.
 */
void answer_parameters(std::uint64_t sessionId,
                       std::uint64_t requested,
                       std::uint8_t playerCount) noexcept;

} // namespace sunrise::server::gameplay::group
