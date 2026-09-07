#include "gameplay_runtime.h"

#include "actor_command_policy.h"
#include "association/association_host.h"
#include "dtls/dtls_host.h"
#include "endpoint/gameplay_endpoint.h"
#include "group/group_host.h"
#include "peer/peer_transport.h"
#include "squad_entity_retirement.h"

namespace sunrise::server::gameplay {

/** Binds the gameplay endpoint for the configured topology. */
bool initialize() noexcept {
    squad_entity_retirement::reset();
    association::reset();
    dtls::reset();
    peer::reset();
    group::reset();
    actor_command_policy::initialize();
    if (endpoint::initialize()) {
        return true;
    }
    actor_command_policy::shutdown();
    squad_entity_retirement::reset();
    return false;
}

/** Runs one bounded gameplay slice. */
void service(std::uint64_t now) noexcept {
    endpoint::service(now);
    // The retries run before the send so anything they queue leaves in this slice.
    group::service(now);
    actor_command_policy::service(now);
    peer::service(now);
}

/** Stops the endpoint and clears every association and peer. */
void shutdown() noexcept {
    endpoint::shutdown();
    actor_command_policy::shutdown();
    squad_entity_retirement::reset();
    peer::reset();
    group::reset();
    dtls::reset();
    association::reset();
}

} // namespace sunrise::server::gameplay
