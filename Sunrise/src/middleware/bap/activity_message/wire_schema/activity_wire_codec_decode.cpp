#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "../../../encoding/bit_reader.h"
#include "activity_wire_codec.h"
#include "activity_wire_codec_decode_runtime.h"
#include "activity_wire_codec_internal.h"

// The reader half of the wire codec. One walk over a message's field graph, two
// sinks that retain what the selected-schema reader decodes, and the three public entry
// points that drive them.

namespace sunrise::middleware::bap::activity_message::wire_schema {
namespace {

/** @return The stored value one raw field carries, with its encode bias removed. */
[[nodiscard]] std::uint64_t decode_integer(std::uint64_t raw,
                                           const FieldDescriptor& field) noexcept {
    const std::uint8_t width = storage_width(field.typeCode);
    return (raw - static_cast<std::uint64_t>(field.bias)) & mask(width);
}

/** Appends selected values to one generic message decode under its exact outer occurrence. */
class MessageRuntimeDecodeSink final {
public:
    MessageRuntimeDecodeSink(DecodedPacket& output,
                             std::uint16_t outerField,
                             std::uint32_t outerElement,
                             FieldExposure exposure) noexcept
        : output_(output), outerField_(outerField), outerElement_(outerElement),
          exposure_(exposure) {}

    /** Records one decoded field occurrence in the packet output. */
    void append(const runtime::SchemaView& schema,
                const runtime::FieldView& field,
                std::uint32_t occurrence,
                std::uint32_t bitOffset,
                std::uint8_t width,
                std::uint64_t decoded,
                bool present,
                ValueRole role) noexcept {
        if (exposure_ == FieldExposure::redacted) {
            output_.valuesRedacted = true;
            return;
        }
        if (output_.valueCount == output_.values.size()) {
            output_.valuesTruncated = true;
            return;
        }
        DecodedValue& value = output_.values[output_.valueCount++];
        value.fieldIndex = outerField_;
        value.element = outerElement_;
        value.bitOffset = bitOffset;
        value.selectedSchemaHandle = schema.handle;
        value.selectedSchemaRow = schema.row;
        value.selectedFieldRow = field.row;
        value.selectedOccurrence = occurrence;
        value.width = width;
        value.role = role;
        value.present = present;
        fill_runtime_role_value(value, field, decoded, role);
    }

private:
    DecodedPacket& output_;
    std::uint16_t outerField_{};
    std::uint32_t outerElement_{};
    FieldExposure exposure_{FieldExposure::redacted};
};

/** Appends selected values to caller-owned standalone storage with full count accounting. */
class StandaloneRuntimeDecodeSink final {
public:
    StandaloneRuntimeDecodeSink(std::span<RuntimeDecodedValue> values,
                                RuntimeDecodeResult& result) noexcept
        : values_(values), result_(result) {}

    /** Records one decoded field occurrence in the value result. */
    void append(const runtime::SchemaView& schema,
                const runtime::FieldView& field,
                std::uint32_t occurrence,
                std::uint32_t bitOffset,
                std::uint8_t width,
                std::uint64_t decoded,
                bool present,
                ValueRole role) noexcept {
        ++result_.requiredValueCount;
        if (result_.valueCount == values_.size()) {
            result_.valuesTruncated = true;
            return;
        }
        RuntimeDecodedValue& value = values_[result_.valueCount++];
        value = {};
        value.schemaHandle = schema.handle;
        value.schemaRow = schema.row;
        value.fieldRow = field.row;
        value.occurrence = occurrence;
        value.bitOffset = bitOffset;
        value.width = width;
        value.role = role;
        value.present = present;
        fill_runtime_role_value(value, field, decoded, role);
    }

private:
    std::span<RuntimeDecodedValue> values_;
    RuntimeDecodeResult& result_;
};

/** Reflection reader shared by all fixed graph messages. */
class Decoder final {
public:
    Decoder(std::span<const FieldDescriptor> fields,
            std::span<const std::byte> payload,
            DecodedPacket& output,
            std::size_t initialBitOffset = 0,
            const RuntimeSchemaResolver* resolver = nullptr) noexcept
        : fields_(fields), reader_(payload), totalBits_(payload.size() * 8), output_(output),
          resolver_(resolver), ready_(reader_.skip(initialBitOffset)) {
        ready_ = ready_ && memory_.select(fields_);
    }

    [[nodiscard]] bool walk() noexcept {
        return ready_ && walk_sequence(0, fields_.size());
    }

    [[nodiscard]] std::size_t position() const noexcept {
        return totalBits_ - reader_.remaining_bits();
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return reader_.remaining_bits();
    }

    /** Consumes and validates zero byte padding at the end of the body. */
    [[nodiscard]] bool finish_padding() noexcept {
        const std::size_t remainingBits = reader_.remaining_bits();
        if (remainingBits == 0) {
            output_.status = CodecStatus::complete;
            return true;
        }
        if (remainingBits >= 8) {
            return false;
        }
        std::uint64_t padding = 0;
        if (!reader_.read(static_cast<std::uint8_t>(remainingBits), padding) || padding != 0) {
            return false;
        }
        output_.status = CodecStatus::completeWithPadding;
        return true;
    }

private:
    /** Walks each immediate reflection subtree in declaration order. */
    [[nodiscard]] bool walk_sequence(std::size_t first, std::size_t end) noexcept {
        for (std::size_t index = first; index < end;) {
            const std::size_t next = subtree_end(fields_, index, end);
            if (!walk_field(index, next, -1)) {
                return false;
            }
            index = next;
        }
        return true;
    }

    /** Decodes one scalar or nested field, including every flattened occurrence. */
    [[nodiscard]] bool
    walk_field(std::size_t index, std::size_t end, std::int64_t repeatOverride) noexcept {
        const FieldDescriptor& field = fields_[index];
        const std::int64_t repeats =
            repeatOverride >= 0 ? repeatOverride : (std::max)(1, static_cast<int>(field.repeat));
        if (field.typeCode == 34 || field.typeCode == 41) {
            return walk_selected_field(field, repeats);
        }
        if (field.presenceBit) {
            const std::uint32_t at = static_cast<std::uint32_t>(position());
            std::uint64_t present = 0;
            if (!reader_.read(1, present)) {
                return false;
            }
            if (present == 0) {
                if (!append(field, at, 0, 0, false) || !advance_occurrences(field, repeats - 1)) {
                    return false;
                }
                return field.typeCode != 1
                       || reserve_sequence(index + 1, end, static_cast<std::uint64_t>(repeats));
            }
            if (field.typeCode == 1 && !append(field, at, 1, 1, true)) {
                return false;
            }
            if (field.typeCode == 1 && !advance_occurrences(field, repeats - 1)) {
                return false;
            }
        }
        if (field.typeCode == 1) {
            return walk_nested(index, end, repeats);
        }
        const bool raw64 = field.typeCode == 35;
        const std::uint8_t nativeWidth = raw64 ? 64 : storage_width(field.typeCode);
        if (nativeWidth == 0) {
            output_.status = CodecStatus::unsupportedField;
            return false;
        }
        const std::int32_t declaredWidth = raw64                  ? 64
                                           : field.typeCode == 2  ? 1
                                           : field.typeCode == 11 ? nativeWidth
                                                                  : field.widthOrCountOffset;
        if (declaredWidth <= 0 || declaredWidth > 64) {
            output_.status = CodecStatus::unsupportedField;
            return false;
        }
        for (std::int64_t element = 0; element < repeats; ++element) {
            const std::uint32_t at = static_cast<std::uint32_t>(position());
            std::uint64_t raw = 0;
            if (!reader_.read(static_cast<std::uint8_t>(declaredWidth), raw)) {
                return false;
            }
            const std::uint64_t decoded =
                raw64 || field.typeCode == 11 ? raw : decode_integer(raw, field);
            if (!append(field, at, static_cast<std::uint8_t>(declaredWidth), decoded, true)) {
                return false;
            }
            const std::int64_t memoryValue = signed_type(field.typeCode)
                                                 ? sign_extend(decoded, nativeWidth)
                                                 : static_cast<std::int64_t>(decoded);
            const std::int32_t stride = (std::max)(1, static_cast<int>(nativeWidth / 8));
            memory_.store(field.absoluteStructOffset + static_cast<std::int32_t>(element) * stride,
                          memoryValue);
        }
        return true;
    }

    /** Decodes one static type-34/type-41 wrapper and its uniquely selected full body. */
    [[nodiscard]] bool walk_selected_field(const FieldDescriptor& field,
                                           std::int64_t repeats) noexcept {
        if (resolver_ == nullptr || resolver_->findSchema == nullptr
            || resolver_->readSchema == nullptr || resolver_->readField == nullptr) {
            output_.status = CodecStatus::needsRuntimeSchema;
            return false;
        }
        for (std::int64_t index = 0; index < repeats; ++index) {
            if (field.catalogIndex >= occurrences_.size()) {
                output_.status = CodecStatus::unsupportedField;
                return false;
            }
            const std::uint32_t occurrence = occurrences_[field.catalogIndex]++;
            if (field.presenceBit) {
                const std::uint32_t at = static_cast<std::uint32_t>(position());
                std::uint64_t outerPresent = 0;
                if (!reader_.read(1, outerPresent)) {
                    return false;
                }
                if (!append_occurrence(field,
                                       occurrence,
                                       at,
                                       1,
                                       outerPresent,
                                       outerPresent != 0,
                                       ValueRole::fieldPresence)) {
                    return false;
                }
                if (outerPresent == 0) {
                    continue;
                }
            }
            const std::uint32_t nullableAt = static_cast<std::uint32_t>(position());
            std::uint64_t selectedPresent = 0;
            if (!reader_.read(1, selectedPresent)) {
                return false;
            }
            if (selectedPresent == 0) {
                if (!append_occurrence(
                        field, occurrence, nullableAt, 0, 0, false, ValueRole::selectedHandle)) {
                    return false;
                }
                continue;
            }
            std::uint64_t rawHandle = 0;
            if (!reader_.read(32, rawHandle)
                || !append_occurrence(field,
                                      occurrence,
                                      nullableAt + 1,
                                      32,
                                      rawHandle,
                                      true,
                                      ValueRole::selectedHandle)) {
                return false;
            }
            const auto handle = static_cast<std::uint32_t>(rawHandle);
            if (output_.selectedSchema == 0 && handle != runtime::kAbsentRuntimeRow) {
                output_.selectedSchema = handle;
            }
            if (handle == runtime::kAbsentRuntimeRow) {
                continue;
            }
            runtime::SchemaView schema{};
            if (!resolver_->findSchema(resolver_->context, handle, schema)
                || schema.handle != handle || !valid_runtime_schema(schema)) {
                output_.status = CodecStatus::needsRuntimeSchema;
                return false;
            }
            RuntimeBitReader bounded(reader_, reader_.remaining_bits(), totalBits_);
            MessageRuntimeDecodeSink sink(output_, field.catalogIndex, occurrence, field.exposure);
            RuntimeSchemaDecoder decoder(*resolver_, bounded, sink);
            const RuntimeWalkStatus status = decoder.full(schema);
            if (status != RuntimeWalkStatus::complete) {
                output_.status = codec_status(status);
                return false;
            }
        }
        return true;
    }

    /** Resolves fixed or count-backed nested repetitions before visiting their children. */
    [[nodiscard]] bool
    walk_nested(std::size_t index, std::size_t end, std::int64_t repeats) noexcept {
        const FieldDescriptor& field = fields_[index];
        if (index + 1 >= end) {
            output_.status = CodecStatus::unsupportedField;
            return false;
        }
        std::int64_t dynamicCount = -1;
        if (field.bias == 1) {
            const std::int32_t base = field.absoluteStructOffset - field.structOffset;
            if (!memory_.find(base + field.widthOrCountOffset, dynamicCount) || dynamicCount < 0) {
                output_.status = CodecStatus::unsafeCount;
                return false;
            }
        }
        std::size_t firstChild = end;
        const std::size_t childCount = direct_children(fields_, index, end, firstChild);
        if (childCount == 0 || firstChild == end) {
            output_.status = CodecStatus::unsupportedField;
            return false;
        }
        if (dynamicCount >= 0) {
            const std::uint16_t capacity = (std::max)(std::uint16_t{1}, fields_[firstChild].repeat);
            if (dynamicCount > capacity || childCount != 1) {
                output_.status = CodecStatus::unsafeCount;
                return false;
            }
            const std::size_t childEnd = subtree_end(fields_, firstChild, end);
            if (dynamicCount != 0 && !walk_field(firstChild, childEnd, dynamicCount)) {
                return false;
            }
            return reserve_field(
                firstChild, childEnd, 1, static_cast<std::int64_t>(capacity) - dynamicCount);
        }
        for (std::int64_t element = 0; element < repeats; ++element) {
            if (!walk_sequence(index + 1, end)) {
                return false;
            }
        }
        return true;
    }

    /** Reserves a skipped sequence's declared row-major occurrence capacity. */
    [[nodiscard]] bool
    reserve_sequence(std::size_t first, std::size_t end, std::uint64_t outer) noexcept {
        for (std::size_t index = first; index < end;) {
            const std::size_t next = subtree_end(fields_, index, end);
            if (!reserve_field(index, next, outer, -1)) {
                return false;
            }
            index = next;
        }
        return true;
    }

    /** Advances a skipped field and all descendants without retaining synthetic values. */
    [[nodiscard]] bool reserve_field(std::size_t index,
                                     std::size_t end,
                                     std::uint64_t outer,
                                     std::int64_t repeatOverride) noexcept {
        const FieldDescriptor& field = fields_[index];
        const std::uint64_t repeats =
            repeatOverride >= 0
                ? static_cast<std::uint64_t>(repeatOverride)
                : (std::max)(std::uint64_t{1}, static_cast<std::uint64_t>(field.repeat));
        if (repeats != 0 && outer > (std::numeric_limits<std::uint32_t>::max)() / repeats) {
            output_.status = CodecStatus::unsafeCount;
            return false;
        }
        const std::uint64_t logical = outer * repeats;
        if ((field.typeCode != 1 || field.presenceBit)
            && !advance_occurrences(field, static_cast<std::int64_t>(logical))) {
            return false;
        }
        if (field.typeCode != 1) {
            return true;
        }
        if (index + 1 >= end) {
            output_.status = CodecStatus::unsupportedField;
            return false;
        }
        return reserve_sequence(index + 1, end, logical);
    }

    /** Reserves flattened occurrence identities omitted by one absent or aggregate row. */
    [[nodiscard]] bool advance_occurrences(const FieldDescriptor& field,
                                           std::int64_t count) noexcept {
        if (count < 0 || field.catalogIndex >= occurrences_.size()
            || static_cast<std::uint64_t>(count) > (std::numeric_limits<std::uint32_t>::max)()
                                                       - occurrences_[field.catalogIndex]) {
            output_.status = CodecStatus::unsafeCount;
            return false;
        }
        occurrences_[field.catalogIndex] += static_cast<std::uint32_t>(count);
        return true;
    }

    /** Retains one decoded public field while advancing redacted occurrence identities. */
    [[nodiscard]] bool append(const FieldDescriptor& field,
                              std::uint32_t bitOffset,
                              std::uint8_t width,
                              std::uint64_t value,
                              bool present,
                              ValueRole role = ValueRole::scalar) noexcept {
        if (field.catalogIndex >= occurrences_.size()) {
            output_.status = CodecStatus::unsupportedField;
            return false;
        }
        const std::uint32_t element = occurrences_[field.catalogIndex]++;
        return append_occurrence(field, element, bitOffset, width, value, present, role);
    }

    /** Retains one fixed-field role without advancing its already selected occurrence. */
    [[nodiscard]] bool append_occurrence(const FieldDescriptor& field,
                                         std::uint32_t element,
                                         std::uint32_t bitOffset,
                                         std::uint8_t width,
                                         std::uint64_t value,
                                         bool present,
                                         ValueRole role) noexcept {
        if (field.exposure == FieldExposure::redacted) {
            output_.valuesRedacted = true;
            return true;
        }
        if (output_.valueCount == output_.values.size()) {
            output_.valuesTruncated = true;
            return true;
        }
        DecodedValue& decoded = output_.values[output_.valueCount++];
        decoded.fieldIndex = field.catalogIndex;
        decoded.element = element;
        decoded.bitOffset = bitOffset;
        decoded.width = width;
        decoded.role = role;
        decoded.present = present;
        decoded.unsignedValue = value;
        if (role == ValueRole::fieldPresence || field.typeCode == 2) {
            decoded.kind = ValueKind::boolean;
        } else if (field.typeCode == 11) {
            decoded.kind = ValueKind::real32;
            decoded.realValue = std::bit_cast<float>(static_cast<std::uint32_t>(value));
        } else if (signed_type(field.typeCode)) {
            decoded.kind = ValueKind::signedInteger;
            decoded.signedValue = sign_extend(value, storage_width(field.typeCode));
        }
        return true;
    }

    std::span<const FieldDescriptor> fields_;
    bits::Reader reader_;
    std::size_t totalBits_{};
    DecodedPacket& output_;
    const RuntimeSchemaResolver* resolver_{};
    MemoryMap memory_{};
    std::array<std::uint32_t, kFieldCount> occurrences_{};
    bool ready_{};
};

} // namespace

/** Decodes one fixed reflection message without retaining its raw body. */
bool decode(const MessageDescriptor& message,
            std::span<const std::byte> payload,
            DecodedPacket& output) noexcept {
    const RuntimeSchemaResolver unavailable{};
    return decode(message, payload, unavailable, output);
}

/** Decodes one reflection message, resolving every selected body from the supplied catalog. */
bool decode(const MessageDescriptor& message,
            std::span<const std::byte> payload,
            const RuntimeSchemaResolver& resolver,
            DecodedPacket& output) noexcept {
    output = {};
    const bool selectedRoot = runtime_selected_root(message);
    if (message.layout != LayoutKind::reflection && !selectedRoot) {
        output.status = layout_status(message.layout);
        return false;
    }
    std::size_t initialBitOffset = 0;
    if (message.callForm == CallForm::deltaRootBit) {
        bits::Reader rootReader(payload);
        std::uint64_t present = 0;
        if (!rootReader.read(1, present)) {
            output.status = CodecStatus::malformed;
            return false;
        }
        output.rootPresent = present != 0;
        if (!output.rootPresent) {
            const std::size_t paddingBits = rootReader.remaining_bits();
            std::uint64_t padding = 0;
            const bool valid = paddingBits < 8
                               && rootReader.read(static_cast<std::uint8_t>(paddingBits), padding)
                               && padding == 0;
            output.bitsConsumed = valid ? payload.size() * 8 : 1;
            output.bitsRemaining = valid ? 0 : paddingBits;
            output.status = valid ? CodecStatus::completeWithPadding : CodecStatus::malformed;
            return valid;
        }
        initialBitOffset = 1;
    }
    Decoder decoder(fields(message), payload, output, initialBitOffset, &resolver);
    const bool walked = decoder.walk();
    output.bitsConsumed = decoder.position();
    output.bitsRemaining = decoder.remaining();
    if (!walked) {
        return false;
    }
    const bool closed = decoder.finish_padding();
    output.bitsConsumed = decoder.position();
    output.bitsRemaining = decoder.remaining();
    if (!closed) {
        output.status = CodecStatus::malformed;
    }
    return closed;
}

/** Decodes one exact full runtime schema body into caller-bounded value storage. */
bool decode_full_schema(std::uint32_t schemaHandle,
                        std::span<const std::byte> payload,
                        std::size_t bitCount,
                        const RuntimeSchemaResolver& resolver,
                        std::span<RuntimeDecodedValue> values,
                        RuntimeDecodeResult& result) noexcept {
    result = {};
    const bool hasResolver = resolver.findSchema != nullptr && resolver.readSchema != nullptr
                             && resolver.readField != nullptr;
    if (values.size() > kRuntimeValueCapacity || bitCount > payload.size() * 8
        || payload.size() != (bitCount + 7) / 8 || !hasResolver) {
        result.status = hasResolver ? CodecStatus::malformed : CodecStatus::needsRuntimeSchema;
        return false;
    }
    if (bitCount % 8 != 0 && !payload.empty()) {
        const auto paddingWidth = static_cast<std::uint8_t>(8 - bitCount % 8);
        const auto paddingMask = static_cast<std::uint8_t>((1U << paddingWidth) - 1U);
        if ((std::to_integer<std::uint8_t>(payload.back()) & paddingMask) != 0) {
            result.status = CodecStatus::malformed;
            return false;
        }
    }
    runtime::SchemaView schema{};
    if (!resolver.findSchema(resolver.context, schemaHandle, schema)
        || schema.handle != schemaHandle || !valid_runtime_schema(schema)) {
        result.status = CodecStatus::needsRuntimeSchema;
        return false;
    }
    bits::Reader reader(payload);
    RuntimeBitReader bounded(reader, bitCount, payload.size() * 8);
    StandaloneRuntimeDecodeSink sink(values, result);
    RuntimeSchemaDecoder decoder(resolver, bounded, sink);
    const RuntimeWalkStatus status = decoder.full(schema);
    result.bitsConsumed = bitCount - bounded.remaining();
    result.bitsRemaining = bounded.remaining();
    if (status != RuntimeWalkStatus::complete || bounded.remaining() != 0) {
        result.status =
            status == RuntimeWalkStatus::complete ? CodecStatus::malformed : codec_status(status);
        return false;
    }
    result.status = bitCount % 8 == 0 ? CodecStatus::complete : CodecStatus::completeWithPadding;
    return true;
}

/**
 * Decodes one runtime schema body off the front of a reader, leaving the tail unread.
 * @param schemaHandle Runtime schema the body must declare.
 * @param reader Advanced past the body only on success.
 * @param resolver Runtime schema lookup callbacks. All three must be set.
 * @param values Caller-bounded value storage.
 * @param result Cleared first. Receives the status and consumed bit counts.
 * @return True when the whole body decoded.
 */
bool decode_full_schema_prefix(std::uint32_t schemaHandle,
                               bits::Reader& reader,
                               const RuntimeSchemaResolver& resolver,
                               std::span<RuntimeDecodedValue> values,
                               RuntimeDecodeResult& result) noexcept {
    result = {};
    if (values.size() > kRuntimeValueCapacity || resolver.findSchema == nullptr
        || resolver.readSchema == nullptr || resolver.readField == nullptr) {
        result.status = CodecStatus::needsRuntimeSchema;
        return false;
    }
    runtime::SchemaView schema{};
    if (!resolver.findSchema(resolver.context, schemaHandle, schema)
        || schema.handle != schemaHandle || !valid_runtime_schema(schema)) {
        result.status = CodecStatus::needsRuntimeSchema;
        return false;
    }
    bits::Reader candidate = reader;
    const std::size_t available = candidate.remaining_bits();
    RuntimeBitReader bounded(candidate, available, available);
    StandaloneRuntimeDecodeSink sink(values, result);
    RuntimeSchemaDecoder decoder(resolver, bounded, sink);
    const RuntimeWalkStatus status = decoder.full(schema);
    result.bitsConsumed = available - bounded.remaining();
    result.bitsRemaining = bounded.remaining();
    if (status != RuntimeWalkStatus::complete) {
        result.status = codec_status(status);
        return false;
    }
    reader = candidate;
    result.status = CodecStatus::complete;
    return true;
}

} // namespace sunrise::middleware::bap::activity_message::wire_schema
