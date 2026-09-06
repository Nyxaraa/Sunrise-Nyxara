#pragma once
#include <cmath>
#include <cstdint>
#include <span>
#include "../../../middleware/bap/activity_message/sense_update.h"
namespace sunrise::server::activity::mission {
struct GhostLevel final {
    std::int32_t generation{};
    float progress{};
    std::uint8_t present{};
    bool active{};
};
// E4A590 publishes active, elapsed/required duration, and the accepted Auth generation.
[[nodiscard]] inline bool update_ghost_level(
    GhostLevel& level,
    std::span<const middleware::bap::activity_message::sense_update::DecodedValue> values,
    std::uint32_t root) noexcept {
    GhostLevel next = level;
    for (const auto& v : values) {
        if (!v.present || v.schemaRow != root) continue;
        switch (v.fieldOrdinal) {
        case 0: next.active = v.unsignedValue != 0; next.present |= 1; break;
        case 1:
            if (!std::isfinite(v.realValue) || v.realValue < 0) return false;
            next.progress = v.realValue; next.present |= 2; break;
        case 2: next.generation = static_cast<std::int32_t>(v.signedValue); next.present |= 4; break;
        default: break;
        }
    }
    const bool changed = next.present != level.present || next.active != level.active
                         || next.progress != level.progress || next.generation != level.generation;
    level = next;
    return changed && next.present == 7;
}
} // namespace sunrise::server::activity::mission
