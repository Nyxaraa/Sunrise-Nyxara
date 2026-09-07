#pragma once

#include <cstdint>

#include "../../middleware/gameplay/external/simulation_event_runtime_codec.h"

namespace sunrise::server::gameplay::actor_command_policy::internal {

/** Reads the unique live target token from a typed DAMAGE event. */
[[nodiscard]] bool damage_target(const middleware::gameplay::external::DecodedRuntimeEvent& event,
                                 middleware::gameplay::external::EntityToken& output) noexcept;

/** Decodes one retained arena record through its SDK event definition. */
[[nodiscard]] bool
decode_event(const middleware::gameplay::external::ActorCommandCatalog& catalog,
             const middleware::gameplay::external::SimulationEventBatch& batch,
             const middleware::gameplay::external::SimulationEventRecord& record,
             middleware::gameplay::external::DecodedRuntimeEvent& output) noexcept;

/** @return True after the session-aware entity transport owns a valid SDK catalog. */
[[nodiscard]] bool entity_transport_ready() noexcept;

/** Removes peer callbacks and clears every retained channel-2 baseline. */
void shutdown_entity_transport() noexcept;

/** Clears transport-scoped actor state while retaining the durable group policy. */
void reset_transport_session(std::uint64_t groupSessionId) noexcept;

} // namespace sunrise::server::gameplay::actor_command_policy::internal
