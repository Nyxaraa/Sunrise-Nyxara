#include "entity_position_profile_provider.h"

#include <string_view>

#include "../../state/activity/runtime.h"
#include "../../state/gameplay/external/entity_position_profiles.h"

namespace sunrise::server::gameplay {

/**
 * The admitted activity selects immutable package bounds without reading the client runtime.
 * @param source Admitted source generation.
 * @param cell Native entity cell index.
 * @param output Receives the selector grammar and any validated widths.
 * @return True; an unknown profile still permits the raw-position selector arm.
 */
bool resolve_entity_position_profile(
    const void*,
    const state::gameplay::entity_identity::Source& source,
    std::uint16_t cell,
    middleware::gameplay::external::PositionProfile& output) noexcept {
    output = {};
    output.selectorPresent = true;
    state::activity::SessionBinding binding{};
    if (!state::activity::snapshot_binding(source.activitySessionId, binding)
        || binding.createdRevision != source.activityRevision) {
        return true;
    }
    const auto& destination = binding.destination;
    if (destination.packageNameLength == 0
        || destination.packageNameLength > destination.packageName.size()) {
        return true;
    }
    const std::string_view activity(reinterpret_cast<const char*>(destination.packageName.data()),
                                    destination.packageNameLength);
    output.hasWidths =
        state::gameplay::entity_position_profiles::lookup(activity, cell, output.axisBits);
    return true;
}

} // namespace sunrise::server::gameplay
