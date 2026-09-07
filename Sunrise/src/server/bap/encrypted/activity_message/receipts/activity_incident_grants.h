#pragma once

#include "../../../../../middleware/bap/activity_message/incident.h"

namespace sunrise::server::bap::encrypted::activity_message::receipts {

/**
 * Applies the state change one accepted collectible report implies.
 * @param incident Outer-valid incident whose targets name the reported object.
 */
void apply_incident_grants(
    const middleware::bap::activity_message::incident::Incident& incident) noexcept;

} // namespace sunrise::server::bap::encrypted::activity_message::receipts
