#pragma once

#include <cstdint>
#include <span>

#include "../../../middleware/bap/activity_message/sense_update.h"

namespace sunrise::server::activity::mission {

/** Nested combatant program record: field 0 is the program state, field 1 its revision. */
inline constexpr std::uint32_t kCombatantProgramSchema = 0x80807F6EU;
/** Root ordinals of the type-2 Sense body. */
inline constexpr std::uint16_t kActorGenerationOrdinal = 0;
inline constexpr std::uint16_t kActorDeliveryRevisionOrdinal = 5;
inline constexpr std::uint16_t kActorDeliveryStateOrdinal = 6;
inline constexpr std::uint16_t kActorDeadOrdinal = 10;
/** Which fields a level has seen. */
inline constexpr std::uint8_t kActorSeenGeneration = 0x01;
inline constexpr std::uint8_t kActorSeenRevision = 0x02;
inline constexpr std::uint8_t kActorSeenState = 0x04;
inline constexpr std::uint8_t kActorSeenDead = 0x08;
inline constexpr std::uint8_t kActorSeenDeliveryRevision = 0x10;
inline constexpr std::uint8_t kActorSeenDeliveryState = 0x20;
/** A level reports once the generation, revision, state and death flag have all arrived. */
inline constexpr std::uint8_t kActorSeenCore =
    kActorSeenGeneration | kActorSeenRevision | kActorSeenState | kActorSeenDead;
inline constexpr std::uint8_t kActorSeenDelivery =
    kActorSeenDeliveryRevision | kActorSeenDeliveryState;

/** Last movement and delivery levels seen for one named actor. */
struct ActorPathLevel final {
    std::int32_t generation{};
    std::int32_t revision{};
    std::int32_t state{};
    std::int32_t deliveryRevision{};
    std::int32_t deliveryState{};
    std::uint8_t seen{};
    bool dead{};
    bool operator==(const ActorPathLevel&) const = default;
};

/**
 * Merges one decoded type-2 body into the retained level.
 * @param root Schema row of the body's root.
 * @return True when the level changed and its core fields are all known.
 */
[[nodiscard]] inline bool update_actor_path_level(
    ActorPathLevel& level,
    std::span<const middleware::bap::activity_message::sense_update::DecodedValue> values,
    std::uint32_t root) noexcept {
    ActorPathLevel next = level;
    for (const auto& value : values) {
        if (!value.present) {
            continue;
        }
        if (value.schemaRow == kCombatantProgramSchema) {
            if (value.fieldOrdinal == 0) {
                next.state = static_cast<std::int32_t>(value.signedValue);
                next.seen |= kActorSeenState;
            } else if (value.fieldOrdinal == 1) {
                next.revision = static_cast<std::int32_t>(value.signedValue);
                next.seen |= kActorSeenRevision;
            }
            continue;
        }
        if (value.schemaRow != root) {
            continue;
        }
        switch (value.fieldOrdinal) {
        case kActorGenerationOrdinal:
            next.generation = static_cast<std::int32_t>(value.signedValue);
            next.seen |= kActorSeenGeneration;
            break;
        case kActorDeliveryRevisionOrdinal:
            next.deliveryRevision = static_cast<std::int32_t>(value.signedValue);
            next.seen |= kActorSeenDeliveryRevision;
            break;
        case kActorDeliveryStateOrdinal:
            next.deliveryState = static_cast<std::int32_t>(value.signedValue);
            next.seen |= kActorSeenDeliveryState;
            break;
        case kActorDeadOrdinal:
            next.dead = value.unsignedValue != 0;
            next.seen |= kActorSeenDead;
            break;
        default:
            break;
        }
    }
    const bool changed = next != level;
    level = next;
    return changed && (next.seen & kActorSeenCore) == kActorSeenCore;
}

} // namespace sunrise::server::activity::mission
