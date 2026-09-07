#include "actor_entity_registry.h"

namespace sunrise::middleware::gameplay::external {
namespace {

namespace format = state::activity_sdk::format;

/** @return True when the slot still holds the incarnation a token names. */
[[nodiscard]] bool same_token(const ActorEntitySlot& slot, const EntityToken& token) noexcept {
    return slot.occupied && slot.incarnation == token.incarnation;
}

/**
 * Resolves one RSAT tag to its single actor class.
 * A duplicate tag, or a row whose reverse definition tag disagrees, resolves to nothing.
 * @param catalog Pinned actor-class rows.
 * @param rsatTag Tag taken from a create baseline.
 * @param output Receives the class index.
 * @return True when exactly one consistent row owns that tag.
 */
[[nodiscard]] bool resolve_actor_class(const ActorEntityCatalog& catalog,
                                       std::uint32_t rsatTag,
                                       std::uint32_t& output) noexcept {
    const std::span<const format::ActorClass> actors = catalog.classes;
    std::uint32_t found = format::kAbsentIndex;
    for (std::size_t index = 0; index < actors.size(); ++index) {
        const format::ActorClass& actor = actors[index];
        if (actor.rsatTag != rsatTag) {
            continue;
        }
        if (found != format::kAbsentIndex || actor.definitionTag == 0
            || actor.definitionTag == format::kAbsentIndex
            || actor.rsatReverseDefinitionTag != actor.definitionTag) {
            return false;
        }
        found = static_cast<std::uint32_t>(index);
    }
    if (found == format::kAbsentIndex) {
        return false;
    }
    output = found;
    return true;
}

/** Pins one catalog view. Every slot is cleared when the rows change owner. */
void bind_catalog(ActorEntityRegistry& registry, const ActorEntityCatalog& catalog) noexcept {
    if (registry.catalog == catalog.owner && registry.classData == catalog.classes.data()
        && registry.classCount == catalog.classes.size()) {
        return;
    }
    registry.catalog = catalog.owner;
    registry.classData = catalog.classes.data();
    registry.classCount = catalog.classes.size();
    registry.slots.fill({});
}

} // namespace

/**
 * Applies one decoded channel-2 record without assigning authored-member identity.
 * @param registry Slot table to update.
 * @param catalog Pinned actor-class rows.
 * @param record Decoded record.
 * @return What the record did to the slot it names.
 */
ActorEntityApplyResult apply_actor_entity_record(ActorEntityRegistry& registry,
                                                 const ActorEntityCatalog& catalog,
                                                 const EntityRecord& record) noexcept {
    if (catalog.classes.empty() || record.token.slot > kMaximumEntitySlot
        || record.token.incarnation > kMaximumEntityIncarnation) {
        return ActorEntityApplyResult::invalid;
    }
    bind_catalog(registry, catalog);
    ActorEntitySlot& slot = registry.slots[record.token.slot];

    if (record.flags == entityRemove) {
        if (!same_token(slot, record.token)) {
            return ActorEntityApplyResult::staleToken;
        }
        slot = {};
        return ActorEntityApplyResult::actorRemoved;
    }
    if ((record.flags & entityCreate) != 0) {
        if (record.type != EntityType::sobject) {
            slot = {};
            return ActorEntityApplyResult::nonActor;
        }
        SobjectBaseline baseline{};
        if (!load_sobject_baseline(record.baseline, baseline)) {
            slot = {};
            return ActorEntityApplyResult::invalid;
        }
        std::uint32_t actorClassIndex = format::kAbsentIndex;
        if (!resolve_actor_class(catalog, baseline.rsatTag, actorClassIndex)) {
            slot = {};
            return ActorEntityApplyResult::nonActor;
        }
        slot.actorClassIndex = actorClassIndex;
        slot.rsatTag = baseline.rsatTag;
        slot.incarnation = record.token.incarnation;
        slot.occupied = true;
        return ActorEntityApplyResult::actorCreated;
    }
    return same_token(slot, record.token) ? ActorEntityApplyResult::unchanged
                                          : ActorEntityApplyResult::staleToken;
}

/**
 * Copies current entity tokens for one SDK actor class into caller-owned storage.
 * @param registry Slot table to scan.
 * @param actorClassIndex Class the caller wants.
 * @param output Caller storage.
 * @param count Receives how many tokens were written, or zero when they did not fit.
 * @return True when the class is known and every live token fit.
 */
bool actor_entity_targets(const ActorEntityRegistry& registry,
                          std::uint32_t actorClassIndex,
                          std::span<EntityToken> output,
                          std::size_t& count) noexcept {
    count = 0;
    if (registry.classData == nullptr || actorClassIndex >= registry.classCount) {
        return false;
    }
    for (std::size_t slotIndex = 0; slotIndex < registry.slots.size(); ++slotIndex) {
        const ActorEntitySlot& slot = registry.slots[slotIndex];
        if (!slot.occupied || slot.actorClassIndex != actorClassIndex) {
            continue;
        }
        if (count == output.size()) {
            count = 0;
            return false;
        }
        output[count++] = EntityToken{static_cast<std::uint16_t>(slotIndex), slot.incarnation};
    }
    return true;
}

} // namespace sunrise::middleware::gameplay::external
