#pragma once
#include <cstdint>
#include <span>
#include "../../../middleware/bap/activity_message/sense_update.h"
namespace sunrise::server::activity::mission {
struct ActorPathLevel {
    std::int32_t generation{}, revision{}, state{};
    std::int32_t deliveryRevision{}, deliveryState{};
    std::uint8_t present{};
    bool dead{};
    bool operator==(const ActorPathLevel&) const = default;
};
[[nodiscard]] inline bool update_actor_path_level(ActorPathLevel& level,
    std::span<const middleware::bap::activity_message::sense_update::DecodedValue> values,
    std::uint32_t root) noexcept {
    auto next = level;
    for (const auto& v : values) {
        if (!v.present) continue;
        // Movement is Auth .6 / Sense .3. Delivery has its own revision and state.
        if (v.schemaRow == 0x80807F6EU) {
            if (v.fieldOrdinal == 0) { next.state = static_cast<std::int32_t>(v.signedValue); next.present |= 4; }
            if (v.fieldOrdinal == 1) { next.revision = static_cast<std::int32_t>(v.signedValue); next.present |= 2; }
            continue;
        }
        if (v.schemaRow != root) continue;
        switch (v.fieldOrdinal) {
        case 0: next.generation = static_cast<std::int32_t>(v.signedValue); next.present |= 1; break;
        case 5: next.deliveryRevision = static_cast<std::int32_t>(v.signedValue); next.present |= 16; break;
        case 6: next.deliveryState = static_cast<std::int32_t>(v.signedValue); next.present |= 32; break;
        case 10: next.dead = v.unsignedValue != 0; next.present |= 8; break;
        default: break;
        }
    }
    const bool changed = next != level; level = next;
    return changed && (next.present & 15) == 15;
}
}
