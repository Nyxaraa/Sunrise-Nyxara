#pragma once
#include <cstdint>
#include <span>
#include "../../../middleware/bap/activity_message/sense_update.h"
namespace sunrise::server::activity::mission {
struct ObjectInteractionLevel final {
    std::int32_t generation{};
    bool generationKnown{}, interacted{}, interactionKnown{};
    bool present{}, alive{}, stateKnown{}, ownerKnown{}, hasOwner{};
    std::uint64_t ownerKey{};
};
// Do not combine a new object generation with a stale interaction latch. Replay of
// an accepted true level is silent; the Lua encounter also retains its one-shot receipt.
[[nodiscard]] inline bool update_object_interaction(ObjectInteractionLevel& level,
    std::span<const middleware::bap::activity_message::sense_update::DecodedValue> values,
    std::uint32_t root) noexcept {
    const auto before = level;
    for (const auto& v : values) {
        if (!v.present || v.schemaRow != root || v.fieldOrdinal != 0) continue;
        const auto generation = static_cast<std::int32_t>(v.signedValue);
        if (level.generationKnown && generation < level.generation) return false;
        if (!level.generationKnown || generation != level.generation) level = {};
        level.generation = generation; level.generationKnown = true;
    }
    // Complete native object bodies clear an absent owner subscription; they are not deltas.
    level.ownerKnown = false; level.hasOwner = false; level.ownerKey = 0;
    for (const auto& v : values) {
        if (v.present && v.schemaRow == root && v.fieldOrdinal == 1) level.alive = v.unsignedValue != 0;
        if (v.present && v.schemaRow == root && v.fieldOrdinal == 2) {
            level.present = v.unsignedValue != 0; level.stateKnown = true;
        }
        if (v.present && v.schemaRow == 0x80809ACCU && v.fieldOrdinal == 0) {
            level.ownerKnown = true; level.hasOwner = v.unsignedValue != 0;
        }
        if (v.present && v.schemaRow == 0x80809ACCU && v.fieldOrdinal == 1) level.ownerKey = v.unsignedValue;
        if (v.present && v.schemaRow == 0x80804FB7U && v.fieldOrdinal == 0) {
            level.interacted = v.unsignedValue != 0; level.interactionKnown = true;
        }
    }
    return level.generationKnown && level.generation > 0 && level.interactionKnown && level.interacted
        && (!before.interactionKnown || !before.interacted || before.generation != level.generation);
}
}
