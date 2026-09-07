#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "actor_command_runtime_codec.h"
#include "actor_entity_registry.h"
#include "simulation_event_codec.h"

namespace sunrise::middleware::gameplay::external {

/** One encoded event part stays well inside a lane arena. */
inline constexpr std::size_t kRuntimeEventBodyCapacity = 512;
/** Typed values retained for one decoded event part. */
inline constexpr std::size_t kRetainedEventValueCapacity = 512;

/** One SDK-resolved simulation event identity. */
struct RuntimeEventIdentity final {
    std::uint32_t eventIndex{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t eventType{};
    std::uint32_t primarySchema{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t secondarySchema{state::activity_sdk::format::kAbsentIndex};
};

/** One encoded event part ready for the lane-0 writer callback. */
struct RuntimeEventBody final {
    std::array<std::byte, kRuntimeEventBodyCapacity> bytes{};
    std::size_t byteCount{};
    std::size_t bitCount{};
};

/** One complete SDK-resolved event contribution. */
struct RuntimeEventDraft final {
    RuntimeEventIdentity identity{};
    RuntimeEventBody primary{};
    RuntimeEventBody secondary{};
    bool primaryPresent{};
};

/** Typed values retained for one decoded event part. */
struct RuntimeEventValues final {
    std::array<actor_wire::RuntimeDecodedValue, kRetainedEventValueCapacity> values{};
    std::size_t count{};
    std::size_t bitCount{};
    bool present{};
};

/** One decoded event with no borrowed wire storage. */
struct DecodedRuntimeEvent final {
    RuntimeEventIdentity identity{};
    RuntimeEventValues primary{};
    RuntimeEventValues secondary{};
};

/** Immutable SDK context for pure lane-0 payload callbacks. */
struct RuntimeEventPayloadCodecContext final {
    const ActorCommandCatalog* catalog{};
};

/** Appends one encoded runtime body to the owning lane arena. */
[[nodiscard]] bool append_runtime_event_body(SimulationEventBatch& batch,
                                             const RuntimeEventBody& body,
                                             SimulationEventPayload& output) noexcept;

/** Builds pure SDK-driven primary and secondary callbacks for SimulationEventPayloadCodec. */
[[nodiscard]] SimulationEventPayloadCodec
make_runtime_event_payload_codec(const RuntimeEventPayloadCodecContext& context) noexcept;

/** Finds one unique named event in the pinned published SDK. */
[[nodiscard]] bool resolve_runtime_event(const ActorCommandCatalog& catalog,
                                         std::string_view name,
                                         RuntimeEventIdentity& output) noexcept;

/** Encodes a command event, including the target EntityToken and selected command body. */
[[nodiscard]] bool
encode_actor_message_event(const ActorCommandCatalog& catalog,
                           std::uint32_t eventIndex,
                           std::uint32_t messageIndex,
                           std::uint32_t commandIndex,
                           const EntityToken& target,
                           const ActorCommandHeader& header,
                           std::span<const actor_wire::RuntimeDraftValue> payloadValues,
                           RuntimeEventDraft& output,
                           actor_wire::CodecStatus& status) noexcept;

/** Decodes an event whose SDK schemas contain no selected trailing body. */
[[nodiscard]] bool decode_runtime_event(const ActorCommandCatalog& catalog,
                                        std::uint32_t eventIndex,
                                        std::span<const std::byte> primary,
                                        std::size_t primaryBits,
                                        std::span<const std::byte> secondary,
                                        std::size_t secondaryBits,
                                        DecodedRuntimeEvent& output) noexcept;

/** Decodes one retained raw-arena record on demand. */
[[nodiscard]] bool decode_runtime_event_record(const ActorCommandCatalog& catalog,
                                               std::uint32_t eventIndex,
                                               const SimulationEventBatch& batch,
                                               const SimulationEventRecord& record,
                                               DecodedRuntimeEvent& output) noexcept;

/** Re-encodes one fully typed event without using its original wire storage. */
[[nodiscard]] bool encode_decoded_runtime_event(const ActorCommandCatalog& catalog,
                                                const DecodedRuntimeEvent& decoded,
                                                RuntimeEventDraft& output,
                                                actor_wire::CodecStatus& status) noexcept;

/** How far one retained event has moved through its restore barrier. */
enum class EventReplayPhase : std::uint8_t {
    empty,
    retained,
    restoreQueued,
    replayReady,
};

/** Generic one-event restore barrier. It assigns no actor class or command policy. */
struct EventReplayTransaction final {
    DecodedRuntimeEvent retained{};
    std::uint64_t restoreFrame{};
    EventReplayPhase phase{EventReplayPhase::empty};
};

/** Retains one typed event until the caller queues its restore command. */
[[nodiscard]] bool retain_runtime_event(EventReplayTransaction& transaction,
                                        const DecodedRuntimeEvent& event) noexcept;

/** Starts the mandatory one-service-frame barrier after restore enqueue. */
[[nodiscard]] bool mark_restore_queued(EventReplayTransaction& transaction,
                                       std::uint64_t serviceFrame) noexcept;

/** Advances to replay-ready only after a later service frame. */
[[nodiscard]] bool advance_event_replay(EventReplayTransaction& transaction,
                                        std::uint64_t serviceFrame) noexcept;

/** Re-encodes and consumes one replay-ready typed event. */
[[nodiscard]] bool take_event_replay(EventReplayTransaction& transaction,
                                     const ActorCommandCatalog& catalog,
                                     RuntimeEventDraft& output,
                                     actor_wire::CodecStatus& status) noexcept;

/** Caller-selected actor-class command policy with no mission-specific identity. */
struct ActorClassCommandPolicy final {
    std::uint32_t actorClassIndex{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t eventIndex{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t messageIndex{state::activity_sdk::format::kAbsentIndex};
    std::uint32_t commandIndex{state::activity_sdk::format::kAbsentIndex};
};

/** Encodes the selected actor class's SDK default faction command for one live token. */
[[nodiscard]] bool encode_actor_class_default_command(const ActorCommandCatalog& catalog,
                                                      const ActorClassCommandPolicy& policy,
                                                      const EntityToken& target,
                                                      const ActorCommandHeader& header,
                                                      RuntimeEventDraft& output,
                                                      actor_wire::CodecStatus& status) noexcept;

} // namespace sunrise::middleware::gameplay::external
