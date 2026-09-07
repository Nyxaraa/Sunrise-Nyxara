#pragma once

#include "../../middleware/gameplay/external/composite_entity_codec.h"

namespace sunrise::server::gameplay {

/** The global selector remains present when this source has no validated cell widths. */
[[nodiscard]] bool
resolve_entity_position_profile(const void* context,
                                const state::gameplay::entity_identity::Source& source,
                                std::uint16_t cell,
                                middleware::gameplay::external::PositionProfile& output) noexcept;

} // namespace sunrise::server::gameplay
