#include "simulation_event_runtime_codec.h"

#include <algorithm>
#include <limits>

#include "../../encoding/bit_raw.h"
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::gameplay::external {
namespace {

namespace format = state::activity_sdk::format;
namespace wire = actor_wire;

/** Bits in one byte. */
constexpr std::size_t kByteBits = 8;
/** Field type code of a schema reference, which selects a trailing body. */
constexpr std::uint8_t kSchemaReferenceTypeCode = 23;
/** Field type code of an entity reference, written as kind, slot and incarnation. */
constexpr std::uint8_t kEntityReferenceTypeCode = 18;
/** Entity-reference kind naming a live simulation token. */
constexpr std::uint64_t kEntityReferenceTokenKind = 2;
/** Field type codes in this closed range are signed integers. */
constexpr std::uint8_t kFirstSignedTypeCode = 3;
constexpr std::uint8_t kLastSignedTypeCode = 6;
/** One secondary body never authors more values than this. */
constexpr std::size_t kSecondaryDraftCapacity = 16;

/** @return True when one event row's flags match its two schema handles. */
[[nodiscard]] bool valid_event_definition(const format::SimulationEventDefinition& event) noexcept {
    /** Only these three flags are understood; any other flag refuses the row. */
    constexpr std::uint32_t kAllowedFlags = format::kSimulationEventDefinitionExact
                                            | format::kSimulationEventPrimaryAbsent
                                            | format::kSimulationEventSecondaryAbsent;
    const bool primaryAbsent = event.primarySchema == format::kAbsentIndex;
    const bool secondaryAbsent = event.secondarySchema == format::kAbsentIndex;
    return event.eventType <= kMaximumSimulationEventType
           && event.provenance == format::ActorSemanticProvenance::executableStatic
           && event.descriptorEvidenceAddress != 0
           && (event.flags & format::kSimulationEventDefinitionExact) != 0
           && (event.flags & ~kAllowedFlags) == 0
           && primaryAbsent == ((event.flags & format::kSimulationEventPrimaryAbsent) != 0)
           && secondaryAbsent == ((event.flags & format::kSimulationEventSecondaryAbsent) != 0)
           && (primaryAbsent || event.primaryEvidenceAddress != 0)
           && (secondaryAbsent || event.secondaryEvidenceAddress != 0) && event.reserved == 0;
}

/** Accepts only an exact event row at its published index. */
[[nodiscard]] bool valid_event(const ActorCommandCatalog& catalog,
                               std::uint32_t eventIndex,
                               const format::SimulationEventDefinition*& output) noexcept {
    if (eventIndex >= catalog.events.size()) {
        return false;
    }
    const format::SimulationEventDefinition& event = catalog.events[eventIndex];
    if (!valid_event_definition(event)) {
        return false;
    }
    output = &event;
    return true;
}

/** Joins two bit bodies without introducing byte-alignment padding. */
[[nodiscard]] bool concatenate(RuntimeEventBody& output,
                               std::span<const std::byte> first,
                               std::size_t firstBits,
                               std::span<const std::byte> second,
                               std::size_t secondBits) noexcept {
    if (firstBits + secondBits > output.bytes.size() * kByteBits) {
        return false;
    }
    encoding::bits::Reader firstReader(first);
    encoding::bits::Reader secondReader(second);
    encoding::bits::Writer writer(output.bytes);
    if (!encoding::bits::copy(firstReader, writer, firstBits)
        || !encoding::bits::copy(secondReader, writer, secondBits)) {
        return false;
    }
    output.bitCount = writer.bit_count();
    return writer.finish(output.byteCount);
}

/** Copies one decoded value into its draft form for a re-encode. */
[[nodiscard]] wire::RuntimeDraftValue
draft_from_decoded(const wire::RuntimeDecodedValue& value) noexcept {
    wire::RuntimeDraftValue output{};
    output.schemaHandle = value.schemaHandle;
    output.schemaRow = value.schemaRow;
    output.fieldRow = value.fieldRow;
    output.occurrence = value.occurrence;
    output.role = value.role;
    output.kind = value.kind;
    output.unsignedValue = value.unsignedValue;
    output.signedValue = value.signedValue;
    output.realValue = value.realValue;
    output.present = value.present;
    return output;
}

/** Encodes one complete reflected schema into a bounded event body. */
[[nodiscard]] bool encode_values(std::uint32_t schema,
                                 std::span<const wire::RuntimeDraftValue> values,
                                 const wire::RuntimeSchemaResolver& resolver,
                                 RuntimeEventBody& output,
                                 wire::CodecStatus& status) noexcept {
    output = {};
    return wire::encode_full_schema(
        schema, values, resolver, output.bytes, output.byteCount, output.bitCount, status);
}

/** Decodes one complete reflected body and rejects truncation. */
[[nodiscard]] bool decode_values(std::uint32_t schema,
                                 std::span<const std::byte> bytes,
                                 std::size_t bits,
                                 const wire::RuntimeSchemaResolver& resolver,
                                 RuntimeEventValues& output) noexcept {
    output = {};
    wire::RuntimeDecodeResult result{};
    if (!wire::decode_full_schema(schema, bytes, bits, resolver, output.values, result)
        || result.valuesTruncated || result.valueCount > output.values.size()) {
        return false;
    }
    output.count = result.valueCount;
    output.bitCount = bits;
    output.present = true;
    return true;
}

/** Resolves one unique exact registry row by its five-bit selector. */
[[nodiscard]] const format::SimulationEventDefinition*
event_by_type(const ActorCommandCatalog& catalog, std::uint8_t eventType) noexcept {
    const format::SimulationEventDefinition* found = nullptr;
    for (const format::SimulationEventDefinition& event : catalog.events) {
        if (event.eventType != eventType) {
            continue;
        }
        if (found != nullptr || !valid_event_definition(event)) {
            return nullptr;
        }
        found = &event;
    }
    return found;
}

/** Detects schemas whose body grammar depends on a selected trailing type. */
[[nodiscard]] bool schema_has_type(const ActorCommandCatalog& catalog,
                                   std::uint32_t schemaHandle,
                                   std::uint32_t typeCode) noexcept {
    for (const format::RuntimeSchema& schema : catalog.schemas) {
        if (schema.handle != schemaHandle || schema.fields.first > catalog.fields.size()
            || schema.fields.count > catalog.fields.size() - schema.fields.first) {
            continue;
        }
        const std::span<const format::RuntimeField> fields =
            catalog.fields.subspan(schema.fields.first, schema.fields.count);
        return std::any_of(fields.begin(), fields.end(), [typeCode](const auto& field) noexcept {
            return field.typeCode == typeCode;
        });
    }
    return false;
}

/**
 * Writes only the meaningful body bits and excludes byte padding.
 * TODO: no caller yet. The lane-0 writer that emits a draft is not built.
 */
[[nodiscard]] bool write_body_bits(const RuntimeEventBody& body,
                                   encoding::bits::Writer& writer) noexcept {
    if (body.bitCount > body.byteCount * kByteBits || body.byteCount > body.bytes.size()) {
        return false;
    }
    encoding::bits::Reader reader(std::span(body.bytes).first(body.byteCount));
    return encoding::bits::copy(reader, writer, body.bitCount);
}

/** Validates one fixed body and any primary-selected trailing schema. */
[[nodiscard]] bool read_runtime_payload(const void* raw,
                                        std::uint8_t eventType,
                                        SimulationEventPayloadPart part,
                                        const SimulationEventPayloadView* primary,
                                        encoding::bits::Reader& reader) noexcept {
    const auto* const context = static_cast<const RuntimeEventPayloadCodecContext*>(raw);
    if (context == nullptr || context->catalog == nullptr) {
        return false;
    }
    const format::SimulationEventDefinition* event = event_by_type(*context->catalog, eventType);
    const std::uint32_t schema = event == nullptr ? format::kAbsentIndex
                                 : part == SimulationEventPayloadPart::primary
                                     ? event->primarySchema
                                     : event->secondarySchema;
    if (schema == format::kAbsentIndex) {
        return false;
    }
    const wire::RuntimeSchemaResolver resolver = actor_runtime_schema_resolver(*context->catalog);
    encoding::bits::Reader candidateReader = reader;
    RuntimeEventValues values{};
    wire::RuntimeDecodeResult result{};
    if (!wire::decode_full_schema_prefix(schema, candidateReader, resolver, values.values, result)
        || result.valuesTruncated || result.valueCount > values.values.size()) {
        return false;
    }
    values.count = result.valueCount;
    if (part == SimulationEventPayloadPart::secondary
        && event->primarySchema != format::kAbsentIndex
        && schema_has_type(*context->catalog, event->primarySchema, kSchemaReferenceTypeCode)) {
        RuntimeEventValues primaryValues{};
        if (primary == nullptr || primary->payload.bitOffset > primary->arena.size() * kByteBits
            || primary->payload.bitCount
                   > primary->arena.size() * kByteBits - primary->payload.bitOffset) {
            return false;
        }
        encoding::bits::Reader primaryReader(primary->arena);
        if (!primaryReader.skip(primary->payload.bitOffset)) {
            return false;
        }
        const std::size_t primaryBefore = primaryReader.remaining_bits();
        wire::RuntimeDecodeResult primaryResult{};
        if (!wire::decode_full_schema_prefix(
                event->primarySchema, primaryReader, resolver, primaryValues.values, primaryResult)
            || primaryResult.valuesTruncated
            || primaryResult.valueCount > primaryValues.values.size()
            || primaryBefore - primaryReader.remaining_bits() != primary->payload.bitCount) {
            return false;
        }
        primaryValues.count = primaryResult.valueCount;
        const wire::RuntimeDecodedValue* selected = nullptr;
        for (std::size_t index = 0; index < primaryValues.count; ++index) {
            if (primaryValues.values[index].role == wire::ValueRole::schemaReference
                && primaryValues.values[index].present) {
                if (selected != nullptr) {
                    return false;
                }
                selected = &primaryValues.values[index];
            }
        }
        if (selected == nullptr || values.count == values.values.size()) {
            return false;
        }
        wire::RuntimeDecodeResult selectedResult{};
        if (!wire::decode_full_schema_prefix(static_cast<std::uint32_t>(selected->unsignedValue),
                                             candidateReader,
                                             resolver,
                                             std::span(values.values).subspan(values.count),
                                             selectedResult)
            || selectedResult.valuesTruncated
            || selectedResult.valueCount > values.values.size() - values.count) {
            return false;
        }
    }
    reader = candidateReader;
    return true;
}

} // namespace

/** Moves an authored body into the batch's single raw arena. */
bool append_runtime_event_body(SimulationEventBatch& batch,
                               const RuntimeEventBody& body,
                               SimulationEventPayload& output) noexcept {
    return body.byteCount <= body.bytes.size() && body.bitCount <= body.byteCount * kByteBits
           && append_simulation_event_payload(
               batch, std::span(body.bytes).first(body.byteCount), body.bitCount, output);
}

SimulationEventPayloadCodec
make_runtime_event_payload_codec(const RuntimeEventPayloadCodecContext& context) noexcept {
    return {&context,
            read_runtime_payload,
            kMaximumSimulationEventPayloadBits,
            kMaximumSimulationEventPayloadBits};
}

/** Resolves a unique named event from the pinned catalog. */
bool resolve_runtime_event(const ActorCommandCatalog& catalog,
                           std::string_view name,
                           RuntimeEventIdentity& output) noexcept {
    output = {};
    if (catalog.owner == nullptr || name.empty()) {
        return false;
    }
    const format::SimulationEventDefinition* found = nullptr;
    std::uint32_t foundIndex = format::kAbsentIndex;
    for (std::size_t index = 0; index < catalog.events.size(); ++index) {
        const format::SimulationEventDefinition& row = catalog.events[index];
        if (catalog.owner->string(row.name) != name) {
            continue;
        }
        if (found != nullptr || !valid_event(catalog, static_cast<std::uint32_t>(index), found)) {
            return false;
        }
        found = &row;
        foundIndex = static_cast<std::uint32_t>(index);
    }
    if (found == nullptr) {
        return false;
    }
    output = {foundIndex, found->eventType, found->primarySchema, found->secondarySchema};
    return true;
}

/** Encodes the event wrapper and its selected actor-command body. */
bool encode_actor_message_event(const ActorCommandCatalog& catalog,
                                std::uint32_t eventIndex,
                                std::uint32_t messageIndex,
                                std::uint32_t commandIndex,
                                const EntityToken& target,
                                const ActorCommandHeader& header,
                                std::span<const wire::RuntimeDraftValue> payloadValues,
                                RuntimeEventDraft& output,
                                wire::CodecStatus& status) noexcept {
    output = {};
    status = wire::CodecStatus::needsRuntimeSchema;
    const format::SimulationEventDefinition* event = nullptr;
    if (!valid_event(catalog, eventIndex, event) || event->primarySchema == format::kAbsentIndex
        || event->secondarySchema == format::kAbsentIndex || messageIndex >= catalog.messages.size()
        || target.slot > kMaximumEntitySlot || target.incarnation > kMaximumEntityIncarnation) {
        status = wire::CodecStatus::unsupportedField;
        return false;
    }
    const wire::RuntimeSchemaResolver resolver = actor_runtime_schema_resolver(catalog);
    wire::runtime::SchemaView primarySchema{};
    wire::runtime::FieldView primaryField{};
    wire::runtime::SchemaView secondarySchema{};
    if (!resolver.findSchema(resolver.context, event->primarySchema, primarySchema)
        || primarySchema.fieldCount != 1
        || !resolver.readField(resolver.context, primarySchema.firstField, primaryField)
        || primaryField.typeCode != kSchemaReferenceTypeCode
        || !resolver.findSchema(resolver.context, event->secondarySchema, secondarySchema)) {
        status = wire::CodecStatus::unsupportedField;
        return false;
    }

    wire::RuntimeDraftValue primary{};
    primary.schemaHandle = primarySchema.handle;
    primary.schemaRow = primarySchema.row;
    primary.fieldRow = primaryField.row;
    primary.role = wire::ValueRole::schemaReference;
    primary.kind = wire::ValueKind::unsignedInteger;
    primary.unsignedValue = catalog.messages[messageIndex].definitionHandle;
    primary.present = true;
    if (!encode_values(
            primarySchema.handle, std::span(&primary, 1), resolver, output.primary, status)) {
        return false;
    }

    std::array<wire::RuntimeDraftValue, kSecondaryDraftCapacity> secondaryValues{};
    std::size_t secondaryCount = 0;
    for (std::uint32_t ordinal = 0; ordinal < secondarySchema.fieldCount; ++ordinal) {
        wire::runtime::FieldView field{};
        if (!resolver.readField(resolver.context, secondarySchema.firstField + ordinal, field)) {
            status = wire::CodecStatus::needsRuntimeSchema;
            return false;
        }
        if (field.typeCode == kEntityReferenceTypeCode) {
            for (const auto [role, value] :
                 {std::pair{wire::ValueRole::entityReferenceKind, kEntityReferenceTokenKind},
                  std::pair{wire::ValueRole::entityReferenceSlot,
                            static_cast<std::uint64_t>(target.slot)},
                  std::pair{wire::ValueRole::entityReferenceIncarnation,
                            static_cast<std::uint64_t>(target.incarnation)}}) {
                wire::RuntimeDraftValue& draft = secondaryValues[secondaryCount++];
                draft.schemaHandle = secondarySchema.handle;
                draft.schemaRow = secondarySchema.row;
                draft.fieldRow = field.row;
                draft.role = role;
                draft.kind = wire::ValueKind::unsignedInteger;
                draft.unsignedValue = value;
                draft.present = true;
            }
        } else {
            wire::RuntimeDraftValue& draft = secondaryValues[secondaryCount++];
            draft.schemaHandle = secondarySchema.handle;
            draft.schemaRow = secondarySchema.row;
            draft.fieldRow = field.row;
            const bool signedField =
                field.typeCode >= kFirstSignedTypeCode && field.typeCode <= kLastSignedTypeCode;
            draft.kind =
                signedField ? wire::ValueKind::signedInteger : wire::ValueKind::unsignedInteger;
            draft.present = true;
        }
    }
    RuntimeEventBody outer{};
    if (!encode_values(secondarySchema.handle,
                       std::span(secondaryValues).first(secondaryCount),
                       resolver,
                       outer,
                       status)) {
        return false;
    }

    std::array<std::byte, kRuntimeEventBodyCapacity> command{};
    std::size_t commandBytes = 0;
    std::size_t commandBits = 0;
    ActorCommandIdentity commandIdentity{};
    if (!encode_actor_command_body(catalog,
                                   messageIndex,
                                   commandIndex,
                                   header,
                                   payloadValues,
                                   resolver,
                                   command,
                                   commandBytes,
                                   commandBits,
                                   status,
                                   commandIdentity)
        || !concatenate(output.secondary,
                        std::span(outer.bytes).first(outer.byteCount),
                        outer.bitCount,
                        std::span(command).first(commandBytes),
                        commandBits)) {
        return false;
    }

    output.identity = {eventIndex, event->eventType, event->primarySchema, event->secondarySchema};
    output.primaryPresent = true;
    status = wire::CodecStatus::complete;
    return true;
}

/** Decodes fixed bodies and any primary-selected secondary tail. */
bool decode_runtime_event(const ActorCommandCatalog& catalog,
                          std::uint32_t eventIndex,
                          std::span<const std::byte> primary,
                          std::size_t primaryBits,
                          std::span<const std::byte> secondary,
                          std::size_t secondaryBits,
                          DecodedRuntimeEvent& output) noexcept {
    output = {};
    const format::SimulationEventDefinition* event = nullptr;
    if (!valid_event(catalog, eventIndex, event)) {
        return false;
    }
    const wire::RuntimeSchemaResolver resolver = actor_runtime_schema_resolver(catalog);
    if (event->primarySchema == format::kAbsentIndex) {
        if (primaryBits != 0 || !primary.empty()) {
            return false;
        }
    } else if (!decode_values(
                   event->primarySchema, primary, primaryBits, resolver, output.primary)) {
        return false;
    }
    if (event->secondarySchema == format::kAbsentIndex) {
        return false;
    }
    if (!schema_has_type(catalog, event->primarySchema, kSchemaReferenceTypeCode)) {
        if (!decode_values(
                event->secondarySchema, secondary, secondaryBits, resolver, output.secondary)) {
            return false;
        }
    } else {
        const wire::RuntimeDecodedValue* selected = nullptr;
        for (std::size_t index = 0; index < output.primary.count; ++index) {
            if (output.primary.values[index].role == wire::ValueRole::schemaReference
                && output.primary.values[index].present) {
                if (selected != nullptr) {
                    return false;
                }
                selected = &output.primary.values[index];
            }
        }
        if (selected == nullptr) {
            return false;
        }
        encoding::bits::Reader reader(secondary);
        wire::RuntimeDecodeResult outer{};
        if (!wire::decode_full_schema_prefix(
                event->secondarySchema, reader, resolver, output.secondary.values, outer)
            || outer.valuesTruncated || outer.valueCount > output.secondary.values.size()) {
            return false;
        }
        wire::RuntimeDecodeResult tail{};
        if (!wire::decode_full_schema_prefix(
                static_cast<std::uint32_t>(selected->unsignedValue),
                reader,
                resolver,
                std::span(output.secondary.values).subspan(outer.valueCount),
                tail)
            || tail.valuesTruncated
            || tail.valueCount > output.secondary.values.size() - outer.valueCount
            || secondary.size() * kByteBits - reader.remaining_bits() != secondaryBits) {
            return false;
        }
        output.secondary.count = outer.valueCount + tail.valueCount;
        output.secondary.bitCount = secondaryBits;
        output.secondary.present = true;
    }
    output.identity = {eventIndex, event->eventType, event->primarySchema, event->secondarySchema};
    return true;
}

/** Decodes retained slices only when the policy layer needs typed values. */
bool decode_runtime_event_record(const ActorCommandCatalog& catalog,
                                 std::uint32_t eventIndex,
                                 const SimulationEventBatch& batch,
                                 const SimulationEventRecord& record,
                                 DecodedRuntimeEvent& output) noexcept {
    std::array<std::byte, kSimulationEventLaneByteCapacity> primary{};
    std::array<std::byte, kSimulationEventLaneByteCapacity> secondary{};
    const auto copy = [&batch](const SimulationEventPayload& payload,
                               std::span<std::byte> destination,
                               std::size_t& byteCount) noexcept {
        encoding::bits::Reader source({});
        if (!simulation_event_payload_reader(batch, payload, source)) {
            return false;
        }
        encoding::bits::Writer writer(destination);
        return encoding::bits::copy(source, writer, payload.bitCount) && writer.finish(byteCount);
    };
    std::size_t primaryBytes = 0;
    std::size_t secondaryBytes = 0;
    if ((record.primaryPresent && !copy(record.primary, primary, primaryBytes))
        || !copy(record.secondary, secondary, secondaryBytes)) {
        return false;
    }
    return decode_runtime_event(catalog,
                                eventIndex,
                                std::span(primary).first(primaryBytes),
                                record.primaryPresent ? record.primary.bitCount : 0,
                                std::span(secondary).first(secondaryBytes),
                                record.secondary.bitCount,
                                output);
}

/** Rebuilds an event from typed values instead of retained wire bytes. */
bool encode_decoded_runtime_event(const ActorCommandCatalog& catalog,
                                  const DecodedRuntimeEvent& decoded,
                                  RuntimeEventDraft& output,
                                  wire::CodecStatus& status) noexcept {
    output = {};
    status = wire::CodecStatus::needsRuntimeSchema;
    const format::SimulationEventDefinition* event = nullptr;
    if (!valid_event(catalog, decoded.identity.eventIndex, event)
        || event->eventType != decoded.identity.eventType
        || event->primarySchema != decoded.identity.primarySchema
        || event->secondarySchema != decoded.identity.secondarySchema) {
        status = wire::CodecStatus::unsupportedField;
        return false;
    }
    const wire::RuntimeSchemaResolver resolver = actor_runtime_schema_resolver(catalog);
    std::array<wire::RuntimeDraftValue, kRetainedEventValueCapacity> draft{};
    if (decoded.primary.present) {
        for (std::size_t index = 0; index < decoded.primary.count; ++index) {
            draft[index] = draft_from_decoded(decoded.primary.values[index]);
        }
        if (!encode_values(event->primarySchema,
                           std::span(draft).first(decoded.primary.count),
                           resolver,
                           output.primary,
                           status)) {
            return false;
        }
    } else if (event->primarySchema != format::kAbsentIndex) {
        status = wire::CodecStatus::unsupportedField;
        return false;
    }
    for (std::size_t index = 0; index < decoded.secondary.count; ++index) {
        draft[index] = draft_from_decoded(decoded.secondary.values[index]);
    }
    if (!encode_values(event->secondarySchema,
                       std::span(draft).first(decoded.secondary.count),
                       resolver,
                       output.secondary,
                       status)) {
        return false;
    }
    output.identity = decoded.identity;
    output.primaryPresent = decoded.primary.present;
    status = wire::CodecStatus::complete;
    return true;
}

/** Starts a replay transaction with one immutable typed event. */
bool retain_runtime_event(EventReplayTransaction& transaction,
                          const DecodedRuntimeEvent& event) noexcept {
    if (transaction.phase != EventReplayPhase::empty || !event.secondary.present) {
        return false;
    }
    transaction.retained = event;
    transaction.phase = EventReplayPhase::retained;
    return true;
}

/** Records the frame that queued the state restore. */
bool mark_restore_queued(EventReplayTransaction& transaction, std::uint64_t serviceFrame) noexcept {
    if (transaction.phase != EventReplayPhase::retained
        || serviceFrame == (std::numeric_limits<std::uint64_t>::max)()) {
        return false;
    }
    transaction.restoreFrame = serviceFrame;
    transaction.phase = EventReplayPhase::restoreQueued;
    return true;
}

/** Makes replay eligible only after a later service frame. */
bool advance_event_replay(EventReplayTransaction& transaction,
                          std::uint64_t serviceFrame) noexcept {
    if (transaction.phase != EventReplayPhase::restoreQueued
        || serviceFrame <= transaction.restoreFrame) {
        return false;
    }
    transaction.phase = EventReplayPhase::replayReady;
    return true;
}

/** Re-encodes and consumes one replay-ready transaction. */
bool take_event_replay(EventReplayTransaction& transaction,
                       const ActorCommandCatalog& catalog,
                       RuntimeEventDraft& output,
                       wire::CodecStatus& status) noexcept {
    if (transaction.phase != EventReplayPhase::replayReady
        || !encode_decoded_runtime_event(catalog, transaction.retained, output, status)) {
        return false;
    }
    transaction = {};
    return true;
}

/** Uses the selected actor class's published default faction as payload. */
bool encode_actor_class_default_command(const ActorCommandCatalog& catalog,
                                        const ActorClassCommandPolicy& policy,
                                        const EntityToken& target,
                                        const ActorCommandHeader& header,
                                        RuntimeEventDraft& output,
                                        wire::CodecStatus& status) noexcept {
    if (policy.actorClassIndex >= catalog.profiles.size()
        || policy.commandIndex >= catalog.commands.size()) {
        status = wire::CodecStatus::unsupportedField;
        return false;
    }
    const format::ActorBehaviorProfile& profile = catalog.profiles[policy.actorClassIndex];
    const format::ActorCommandDefinition& command = catalog.commands[policy.commandIndex];
    const wire::RuntimeSchemaResolver resolver = actor_runtime_schema_resolver(catalog);
    wire::runtime::SchemaView payloadSchema{};
    wire::runtime::FieldView payloadField{};
    if (profile.actorClassIndex != policy.actorClassIndex
        || !resolver.findSchema(resolver.context, command.payloadHandle, payloadSchema)
        || payloadSchema.fieldCount != 1
        || !resolver.readField(resolver.context, payloadSchema.firstField, payloadField)
        || payloadField.typeCode < kFirstSignedTypeCode
        || payloadField.typeCode > kLastSignedTypeCode) {
        status = wire::CodecStatus::unsupportedField;
        return false;
    }
    wire::RuntimeDraftValue value{};
    value.schemaHandle = payloadSchema.handle;
    value.schemaRow = payloadSchema.row;
    value.fieldRow = payloadField.row;
    value.kind = wire::ValueKind::signedInteger;
    value.signedValue = profile.defaultFaction;
    value.present = true;
    return encode_actor_message_event(catalog,
                                      policy.eventIndex,
                                      policy.messageIndex,
                                      policy.commandIndex,
                                      target,
                                      header,
                                      std::span(&value, 1),
                                      output,
                                      status);
}

} // namespace sunrise::middleware::gameplay::external
