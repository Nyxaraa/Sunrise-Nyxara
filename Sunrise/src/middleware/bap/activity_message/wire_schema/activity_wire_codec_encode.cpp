#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>

#include "../../../encoding/bit_writer.h"
#include "activity_wire_codec.h"
#include "activity_wire_codec_encode_runtime.h"
#include "activity_wire_codec_internal.h"

// The writer half of the wire codec. One walk over a message's field graph, two
// sources that supply what the selected-schema writer asks for, and the three public entry
// points that drive them. Every encode stages into its own buffer and copies out on success.

namespace sunrise::middleware::bap::activity_message::wire_schema {
namespace {

/** Copies the scalar payload both draft representations carry under the same field names. */
template <typename Draft>
void copy_authored_value(const Draft& draft, RuntimeAuthoredValue& output) noexcept {
    output.unsignedValue = draft.unsignedValue;
    output.signedValue = draft.signedValue;
    output.realValue = draft.realValue;
    output.kind = draft.kind;
    output.present = draft.present;
}

/**
 * Allocates one zeroed staging buffer so a refused encode leaves the caller's output unchanged.
 * @param size Bytes the caller offered. Zero allocates nothing.
 * @param staging Receives the allocation.
 * @param staged Receives a view over the allocation.
 * @return True when the buffer is ready to write into.
 */
[[nodiscard]] bool stage_output(std::size_t size,
                                std::unique_ptr<std::byte[]>& staging,
                                std::span<std::byte>& staged) noexcept {
    if (size != 0) {
        staging.reset(new (std::nothrow) std::byte[size]);
        if (staging == nullptr) {
            return false;
        }
    }
    staged = std::span<std::byte>(staging.get(), size);
    std::fill(staged.begin(), staged.end(), std::byte{});
    return true;
}

/** Exact selected-value lookup for one outer message-field occurrence. */
class MessageRuntimeDraftSource final {
public:
    MessageRuntimeDraftSource(const PacketDraft& draft,
                              std::array<bool, kDraftValueCapacity>& used,
                              std::uint16_t outerField,
                              std::uint32_t outerElement) noexcept
        : draft_(draft), used_(used), outerField_(outerField), outerElement_(outerElement) {}

    /** Takes the draft value for one field occurrence, marking it used. */
    [[nodiscard]] bool take(const runtime::SchemaView& schema,
                            const runtime::FieldView& field,
                            std::uint32_t occurrence,
                            ValueRole role,
                            RuntimeAuthoredValue& output) noexcept {
        for (std::size_t index = 0; index < draft_.valueCount; ++index) {
            const DraftValue& value = draft_.values[index];
            if (!value.assigned || value.fieldIndex != outerField_ || value.element != outerElement_
                || value.selectedSchemaHandle != schema.handle
                || value.selectedSchemaRow != schema.row || value.selectedFieldRow != field.row
                || value.selectedOccurrence != occurrence || value.role != role) {
                continue;
            }
            used_[index] = true;
            copy_authored_value(value, output);
            return true;
        }
        return false;
    }

private:
    const PacketDraft& draft_;
    std::array<bool, kDraftValueCapacity>& used_;
    std::uint16_t outerField_{};
    std::uint32_t outerElement_{};
};

/** Exact selected-value lookup for one caller-owned standalone draft span. */
class StandaloneRuntimeDraftSource final {
public:
    /** Rejects absent handles or rows, and any two values naming one field occurrence. */
    explicit StandaloneRuntimeDraftSource(std::span<const RuntimeDraftValue> values) noexcept
        : values_(values), valid_(values.size() <= kRuntimeValueCapacity) {
        for (std::size_t index = 0; valid_ && index < values_.size(); ++index) {
            const RuntimeDraftValue& value = values_[index];
            valid_ = value.schemaHandle != 0 && value.schemaHandle != kAbsentUnsigned
                     && value.schemaRow != kAbsentUnsigned && value.fieldRow != kAbsentUnsigned;
            for (std::size_t previous = 0; valid_ && previous < index; ++previous) {
                const RuntimeDraftValue& candidate = values_[previous];
                if (candidate.schemaHandle == value.schemaHandle
                    && candidate.schemaRow == value.schemaRow
                    && candidate.fieldRow == value.fieldRow
                    && candidate.occurrence == value.occurrence && candidate.role == value.role) {
                    valid_ = false;
                }
            }
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return valid_;
    }

    /** Takes the standalone draft value for one field occurrence. */
    [[nodiscard]] bool take(const runtime::SchemaView& schema,
                            const runtime::FieldView& field,
                            std::uint32_t occurrence,
                            ValueRole role,
                            RuntimeAuthoredValue& output) noexcept {
        for (std::size_t index = 0; index < values_.size(); ++index) {
            const RuntimeDraftValue& value = values_[index];
            if (value.schemaHandle != schema.handle || value.schemaRow != schema.row
                || value.fieldRow != field.row || value.occurrence != occurrence
                || value.role != role) {
                continue;
            }
            used_[index] = true;
            copy_authored_value(value, output);
            return true;
        }
        return false;
    }

    /** @return True when every draft value was taken exactly once. */
    [[nodiscard]] bool complete() const noexcept {
        if (!valid_) {
            return false;
        }
        for (std::size_t index = 0; index < values_.size(); ++index) {
            if (!used_[index]) {
                return false;
            }
        }
        return true;
    }

private:
    std::span<const RuntimeDraftValue> values_;
    std::array<bool, kRuntimeValueCapacity> used_{};
    bool valid_{};
};

/** Reflection writer that mirrors Decoder's tree walk. */
class Encoder final {
public:
    /** Indexes authored values and rejects duplicate occurrence identities before writing. */
    Encoder(std::span<const FieldDescriptor> fields,
            const PacketDraft& draft,
            bits::Writer& writer,
            CodecStatus& status,
            const RuntimeSchemaResolver* resolver = nullptr) noexcept
        : fields_(fields), draft_(draft), writer_(writer), status_(status), resolver_(resolver) {
        draftValid_ = memory_.select(fields_);
        if (draft.valueCount > draft.values.size()) {
            draftValid_ = false;
            return;
        }
        for (std::size_t index = 0; index < draft.valueCount; ++index) {
            const DraftValue& value = draft.values[index];
            if (!value.assigned) {
                continue;
            }
            const bool noSelectedIdentity = value.selectedSchemaHandle == kAbsentUnsigned
                                            && value.selectedSchemaRow == kAbsentUnsigned
                                            && value.selectedFieldRow == kAbsentUnsigned;
            const bool fullSelectedIdentity = value.selectedSchemaHandle != 0
                                              && value.selectedSchemaHandle != kAbsentUnsigned
                                              && value.selectedSchemaRow != kAbsentUnsigned
                                              && value.selectedFieldRow != kAbsentUnsigned;
            if (!noSelectedIdentity && !fullSelectedIdentity) {
                draftValid_ = false;
                return;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                const DraftValue& candidate = draft.values[previous];
                if (candidate.assigned && candidate.fieldIndex == value.fieldIndex
                    && candidate.element == value.element
                    && candidate.selectedSchemaHandle == value.selectedSchemaHandle
                    && candidate.selectedSchemaRow == value.selectedSchemaRow
                    && candidate.selectedFieldRow == value.selectedFieldRow
                    && candidate.selectedOccurrence == value.selectedOccurrence
                    && candidate.role == value.role) {
                    draftValid_ = false;
                    return;
                }
            }
        }
    }

    /** Walks the schema and encodes every selected field. @return True when the body fit. */
    [[nodiscard]] bool walk() noexcept {
        if (!draftValid_) {
            status_ = CodecStatus::unsupportedField;
            return false;
        }
        if (!walk_sequence(0, fields_.size())) {
            return false;
        }
        for (std::size_t index = 0; index < draft_.valueCount; ++index) {
            const DraftValue& value = draft_.values[index];
            const bool selected = value.selectedSchemaHandle != kAbsentUnsigned;
            if (value.assigned && (selected || value.role != ValueRole::scalar) && !used_[index]) {
                status_ = CodecStatus::unsupportedField;
                return false;
            }
        }
        return true;
    }

private:
    /** Finds one exact authored occurrence; missing rows intentionally encode as zero. */
    [[nodiscard]] const DraftValue* find_draft(std::uint16_t fieldIndex,
                                               std::uint32_t element,
                                               ValueRole role = ValueRole::scalar,
                                               bool markUsed = false) noexcept {
        for (std::size_t index = 0; index < draft_.valueCount; ++index) {
            const DraftValue& value = draft_.values[index];
            if (value.assigned && value.fieldIndex == fieldIndex && value.element == element
                && value.selectedSchemaHandle == kAbsentUnsigned
                && value.selectedSchemaRow == kAbsentUnsigned
                && value.selectedFieldRow == kAbsentUnsigned && value.role == role) {
                used_[index] = used_[index] || markUsed;
                return &value;
            }
        }
        return nullptr;
    }

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

    /** Encodes one scalar or nested field from its exact authored occurrence. */
    [[nodiscard]] bool
    walk_field(std::size_t index, std::size_t end, std::int64_t repeatOverride) noexcept {
        const FieldDescriptor& field = fields_[index];
        if (field.catalogIndex >= occurrences_.size()) {
            status_ = CodecStatus::unsupportedField;
            return false;
        }
        const std::uint32_t firstElement = occurrences_[field.catalogIndex];
        const std::int64_t repeats =
            repeatOverride >= 0 ? repeatOverride : (std::max)(1, static_cast<int>(field.repeat));
        if (field.typeCode == 34 || field.typeCode == 41) {
            return walk_selected_field(field, repeats);
        }
        const DraftValue* const presence = find_draft(field.catalogIndex, firstElement);
        if (field.presenceBit) {
            const bool present = presence != nullptr && presence->present;
            if (!writer_.write(present ? 1U : 0U, 1)) {
                status_ = CodecStatus::outputTooSmall;
                return false;
            }
            if (!present) {
                if (!advance_occurrences(field, repeats)) {
                    return false;
                }
                return field.typeCode != 1
                       || reserve_sequence(index + 1, end, static_cast<std::uint64_t>(repeats));
            }
        }
        if (field.typeCode == 1) {
            if (field.presenceBit && !advance_occurrences(field, repeats)) {
                return false;
            }
            return walk_nested(index, end, repeats);
        }
        const bool raw64 = field.typeCode == 35;
        const std::uint8_t nativeWidth = raw64 ? 64 : storage_width(field.typeCode);
        const std::int32_t declaredWidth = raw64                  ? 64
                                           : field.typeCode == 2  ? 1
                                           : field.typeCode == 11 ? nativeWidth
                                                                  : field.widthOrCountOffset;
        if (nativeWidth == 0 || declaredWidth <= 0 || declaredWidth > 64) {
            status_ = CodecStatus::unsupportedField;
            return false;
        }
        for (std::int64_t element = 0; element < repeats; ++element) {
            const std::uint32_t occurrence = occurrences_[field.catalogIndex]++;
            const DraftValue* value = find_draft(field.catalogIndex, occurrence);
            if (raw64 && (value == nullptr || value->kind != ValueKind::unsignedInteger)) {
                status_ = CodecStatus::unsupportedField;
                return false;
            }
            std::uint64_t decoded = 0;
            if (value != nullptr) {
                if (raw64) {
                    decoded = value->unsignedValue;
                } else if (field.typeCode == 11) {
                    decoded = std::bit_cast<std::uint32_t>(value->realValue);
                } else if (signed_type(field.typeCode)) {
                    decoded = static_cast<std::uint64_t>(value->signedValue) & mask(nativeWidth);
                } else if (field.typeCode == 2) {
                    decoded = value->unsignedValue != 0 ? 1 : 0;
                } else {
                    decoded = value->unsignedValue & mask(nativeWidth);
                }
            }
            const std::uint64_t raw =
                raw64 || field.typeCode == 11
                    ? decoded
                    : (decoded + static_cast<std::uint64_t>(field.bias)) & mask(nativeWidth);
            if ((raw & ~mask(static_cast<std::uint8_t>(declaredWidth))) != 0
                || !writer_.write(raw, static_cast<std::uint8_t>(declaredWidth))) {
                status_ = CodecStatus::outputTooSmall;
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

    /** Encodes one static type-34/type-41 wrapper from explicit identity-bound values. */
    [[nodiscard]] bool walk_selected_field(const FieldDescriptor& field,
                                           std::int64_t repeats) noexcept {
        if (resolver_ == nullptr || resolver_->findSchema == nullptr
            || resolver_->readSchema == nullptr || resolver_->readField == nullptr) {
            status_ = CodecStatus::needsRuntimeSchema;
            return false;
        }
        for (std::int64_t index = 0; index < repeats; ++index) {
            const std::uint32_t occurrence = occurrences_[field.catalogIndex]++;
            if (field.presenceBit) {
                const DraftValue* const outer =
                    find_draft(field.catalogIndex, occurrence, ValueRole::fieldPresence, true);
                if (outer == nullptr || outer->kind != ValueKind::boolean) {
                    status_ = CodecStatus::unsupportedField;
                    return false;
                }
                if (!writer_.write(outer->present ? 1U : 0U, 1)) {
                    status_ = CodecStatus::outputTooSmall;
                    return false;
                }
                if (!outer->present) {
                    continue;
                }
            }
            const DraftValue* const handle =
                find_draft(field.catalogIndex, occurrence, ValueRole::selectedHandle, true);
            if (handle == nullptr || handle->kind != ValueKind::unsignedInteger
                || handle->unsignedValue > (std::numeric_limits<std::uint32_t>::max)()) {
                status_ = CodecStatus::unsupportedField;
                return false;
            }
            if (!writer_.write(handle->present ? 1U : 0U, 1)) {
                status_ = CodecStatus::outputTooSmall;
                return false;
            }
            if (!handle->present) {
                continue;
            }
            const auto selectedHandle = static_cast<std::uint32_t>(handle->unsignedValue);
            if (!writer_.write(selectedHandle, 32)) {
                status_ = CodecStatus::outputTooSmall;
                return false;
            }
            if (selectedHandle == runtime::kAbsentRuntimeRow) {
                continue;
            }
            runtime::SchemaView schema{};
            if (!resolver_->findSchema(resolver_->context, selectedHandle, schema)
                || schema.handle != selectedHandle || !valid_runtime_schema(schema)) {
                status_ = CodecStatus::needsRuntimeSchema;
                return false;
            }
            MessageRuntimeDraftSource source(draft_, used_, field.catalogIndex, occurrence);
            RuntimeSchemaEncoder encoder(*resolver_, writer_, source);
            const RuntimeWalkStatus encoded = encoder.full(schema);
            if (encoded != RuntimeWalkStatus::complete) {
                status_ = codec_status(encoded);
                return false;
            }
        }
        return true;
    }

    /** Advances one field's flattened occurrence counter with an overflow guard. */
    [[nodiscard]] bool advance_occurrences(const FieldDescriptor& field,
                                           std::int64_t count) noexcept {
        if (count < 0 || field.catalogIndex >= occurrences_.size()
            || static_cast<std::uint64_t>(count) > (std::numeric_limits<std::uint32_t>::max)()
                                                       - occurrences_[field.catalogIndex]) {
            status_ = CodecStatus::unsafeCount;
            return false;
        }
        occurrences_[field.catalogIndex] += static_cast<std::uint32_t>(count);
        return true;
    }

    /** Resolves fixed or count-backed nested repetitions before visiting their children. */
    [[nodiscard]] bool
    walk_nested(std::size_t index, std::size_t end, std::int64_t repeats) noexcept {
        const FieldDescriptor& field = fields_[index];
        std::int64_t dynamicCount = -1;
        if (field.bias == 1) {
            const std::int32_t base = field.absoluteStructOffset - field.structOffset;
            if (!memory_.find(base + field.widthOrCountOffset, dynamicCount) || dynamicCount < 0) {
                status_ = CodecStatus::unsafeCount;
                return false;
            }
        }
        std::size_t firstChild = end;
        const std::size_t childCount = direct_children(fields_, index, end, firstChild);
        if (childCount == 0 || firstChild == end) {
            status_ = CodecStatus::unsupportedField;
            return false;
        }
        if (dynamicCount >= 0) {
            const std::uint16_t capacity = (std::max)(std::uint16_t{1}, fields_[firstChild].repeat);
            if (dynamicCount > capacity || childCount != 1) {
                status_ = CodecStatus::unsafeCount;
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

    /** Advances a skipped field and all descendants without reading authored values. */
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
            status_ = CodecStatus::unsafeCount;
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
            status_ = CodecStatus::unsupportedField;
            return false;
        }
        return reserve_sequence(index + 1, end, logical);
    }

    std::span<const FieldDescriptor> fields_;
    const PacketDraft& draft_;
    bits::Writer& writer_;
    CodecStatus& status_;
    const RuntimeSchemaResolver* resolver_{};
    MemoryMap memory_{};
    std::array<std::uint32_t, kFieldCount> occurrences_{};
    std::array<bool, kDraftValueCapacity> used_{};
    bool draftValid_{true};
};

} // namespace

/** Encodes one named reflection draft after checking its layout. */
bool encode(const MessageDescriptor& message,
            const PacketDraft& draft,
            std::span<std::byte> output,
            std::size_t& written,
            std::size_t& writtenBits,
            CodecStatus& status) noexcept {
    const RuntimeSchemaResolver unavailable{};
    return encode(message, draft, unavailable, output, written, writtenBits, status);
}

/** Encodes one reflection draft with catalog-resolved selected bodies. */
bool encode(const MessageDescriptor& message,
            const PacketDraft& draft,
            const RuntimeSchemaResolver& resolver,
            std::span<std::byte> output,
            std::size_t& written,
            std::size_t& writtenBits,
            CodecStatus& status) noexcept {
    written = 0;
    writtenBits = 0;
    status = layout_status(message.layout);
    const bool selectedRoot = runtime_selected_root(message);
    if (message.layout != LayoutKind::reflection && !selectedRoot) {
        return false;
    }
    if (message.callForm == CallForm::deltaRootBit && !draft.rootPresent) {
        for (std::size_t index = 0; index < draft.valueCount; ++index) {
            const DraftValue& value = draft.values[index];
            if (value.assigned
                && (value.selectedSchemaHandle != kAbsentUnsigned
                    || value.role != ValueRole::scalar)) {
                status = CodecStatus::unsupportedField;
                return false;
            }
        }
    }
    std::unique_ptr<std::byte[]> staging;
    std::span<std::byte> staged;
    if (!stage_output(output.size(), staging, staged)) {
        status = CodecStatus::outputTooSmall;
        return false;
    }
    bits::Writer writer(staged);
    if (message.callForm == CallForm::deltaRootBit
        && !writer.write(draft.rootPresent ? 1U : 0U, 1)) {
        status = CodecStatus::outputTooSmall;
        return false;
    }
    if (message.callForm != CallForm::deltaRootBit || draft.rootPresent) {
        Encoder encoder(fields(message), draft, writer, status, &resolver);
        if (!encoder.walk()) {
            return false;
        }
    }
    writtenBits = writer.bit_count();
    if (!writer.finish(written)) {
        status = CodecStatus::outputTooSmall;
        return false;
    }
    status = CodecStatus::complete;
    std::copy_n(staged.begin(), written, output.begin());
    return true;
}

/** Encodes one exact full runtime schema body from an explicit caller-owned draft span. */
bool encode_full_schema(std::uint32_t schemaHandle,
                        std::span<const RuntimeDraftValue> values,
                        const RuntimeSchemaResolver& resolver,
                        std::span<std::byte> output,
                        std::size_t& written,
                        std::size_t& writtenBits,
                        CodecStatus& status) noexcept {
    written = 0;
    writtenBits = 0;
    status = CodecStatus::needsRuntimeSchema;
    StandaloneRuntimeDraftSource source(values);
    runtime::SchemaView schema{};
    if (!source.valid()) {
        status = CodecStatus::unsupportedField;
        return false;
    }
    if (resolver.findSchema == nullptr || resolver.readSchema == nullptr
        || resolver.readField == nullptr
        || !resolver.findSchema(resolver.context, schemaHandle, schema)
        || schema.handle != schemaHandle || !valid_runtime_schema(schema)) {
        return false;
    }
    std::unique_ptr<std::byte[]> staging;
    std::span<std::byte> staged;
    if (!stage_output(output.size(), staging, staged)) {
        status = CodecStatus::outputTooSmall;
        return false;
    }
    bits::Writer writer(staged);
    RuntimeSchemaEncoder encoder(resolver, writer, source);
    const RuntimeWalkStatus encoded = encoder.full(schema);
    if (encoded != RuntimeWalkStatus::complete || !source.complete()) {
        status = encoded == RuntimeWalkStatus::complete ? CodecStatus::unsupportedField
                                                        : codec_status(encoded);
        return false;
    }
    writtenBits = writer.bit_count();
    if (!writer.finish(written)) {
        status = CodecStatus::outputTooSmall;
        writtenBits = 0;
        return false;
    }
    status = CodecStatus::complete;
    std::copy_n(staged.begin(), written, output.begin());
    return true;
}

} // namespace sunrise::middleware::bap::activity_message::wire_schema
