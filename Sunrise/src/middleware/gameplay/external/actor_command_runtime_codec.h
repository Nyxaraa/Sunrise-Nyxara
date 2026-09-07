#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "../../../state/activity_sdk/runtime.h"
#include "../../bap/activity_message/wire_schema/activity_wire_codec.h"

namespace sunrise::middleware::gameplay::external {

namespace actor_wire = bap::activity_message::wire_schema;

/** Immutable actor-message rows pinned by one SDK snapshot. */
struct ActorCommandCatalog final {
    state::activity_sdk::Snapshot owner{};
    std::span<const state::activity_sdk::format::ActorMessageSchema> messages{};
    std::span<const state::activity_sdk::format::ActorCommandDefinition> commands{};
    std::span<const state::activity_sdk::format::SimulationEventDefinition> events{};
    std::span<const state::activity_sdk::format::RuntimeSchema> schemas{};
    std::span<const state::activity_sdk::format::RuntimeField> fields{};
    std::span<const state::activity_sdk::format::ActorBehaviorProfile> profiles{};
};

/** Structural type-36 values which do not belong to the selected payload schema. */
struct ActorCommandHeader final {
    std::uint16_t target{};
    std::uint16_t auxiliary16{};
    std::uint8_t defaultValue{};
    std::uint8_t mode{};
    std::uint8_t targetReference{};
    std::uint8_t auxiliary8{};
};

/** Resolved identity retained with one encoded or decoded command body. */
struct ActorCommandIdentity final {
    std::uint32_t messageIndex{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t commandIndex{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t definitionHandle{};
    std::uint32_t durableKey{};
    std::uint32_t ownerClass{};
    std::uint32_t handlerSlot{};
    std::uint32_t selector{};
    std::uint32_t payloadHandle{};
};

/**
 * Pins one snapshot's SDK actor-message rows for one complete operation.
 * @param snapshot Snapshot the rows are read from and which keeps them alive.
 * @param output Left cleared unless every row family is present.
 * @return True only when every row family the encoder needs is non-empty.
 */
[[nodiscard]] inline bool
published_actor_command_catalog(const state::activity_sdk::Snapshot& snapshot,
                                ActorCommandCatalog& output) noexcept {
    output = {};
    if (snapshot == nullptr) {
        return false;
    }
    ActorCommandCatalog candidate{};
    candidate.owner = snapshot;
    candidate.messages = candidate.owner->actor_message_schemas();
    candidate.commands = candidate.owner->actor_command_definitions();
    candidate.events = candidate.owner->simulation_event_definitions();
    candidate.schemas = candidate.owner->runtime_schemas();
    candidate.fields = candidate.owner->runtime_fields();
    candidate.profiles = candidate.owner->actor_behavior_profiles();
    if (candidate.messages.empty() || candidate.commands.empty() || candidate.events.empty()
        || candidate.schemas.empty() || candidate.fields.empty() || candidate.profiles.empty()) {
        return false;
    }
    output = std::move(candidate);
    return true;
}

/** Pins the currently published SDK actor-message rows for one complete operation. */
[[nodiscard]] inline bool published_actor_command_catalog(ActorCommandCatalog& output) noexcept {
    return published_actor_command_catalog(state::activity_sdk::snapshot(), output);
}

/** Builds a generic reflection resolver over the catalog's exact runtime rows. */
[[nodiscard]] actor_wire::RuntimeSchemaResolver
actor_runtime_schema_resolver(const ActorCommandCatalog& catalog) noexcept;

/**
 * Encodes one SDK-selected actor command body.
 * Payload values must use the command's payload schema identity.
 */
[[nodiscard]] bool
encode_actor_command_body(const ActorCommandCatalog& catalog,
                          std::uint32_t messageIndex,
                          std::uint32_t commandIndex,
                          const ActorCommandHeader& header,
                          std::span<const actor_wire::RuntimeDraftValue> payloadValues,
                          const actor_wire::RuntimeSchemaResolver& reflection,
                          std::span<std::byte> output,
                          std::size_t& written,
                          std::size_t& writtenBits,
                          actor_wire::CodecStatus& status,
                          ActorCommandIdentity& identity) noexcept;

/** Decodes one SDK-selected actor command body and resolves its selector row. */
[[nodiscard]] bool decode_actor_command_body(const ActorCommandCatalog& catalog,
                                             std::uint32_t messageIndex,
                                             std::span<const std::byte> payload,
                                             std::size_t bitCount,
                                             const actor_wire::RuntimeSchemaResolver& reflection,
                                             std::span<actor_wire::RuntimeDecodedValue> values,
                                             actor_wire::RuntimeDecodeResult& result,
                                             ActorCommandIdentity& identity) noexcept;

} // namespace sunrise::middleware::gameplay::external
