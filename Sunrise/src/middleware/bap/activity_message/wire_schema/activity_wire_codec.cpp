#include "activity_wire_codec.h"

#include "activity_wire_codec_internal.h"

namespace sunrise::middleware::bap::activity_message::wire_schema {

/** Maps one runtime walker result to the public codec boundary. */
CodecStatus codec_status(RuntimeWalkStatus status) noexcept {
    switch (status) {
    case RuntimeWalkStatus::complete:
        return CodecStatus::complete;
    case RuntimeWalkStatus::malformed:
        return CodecStatus::malformed;
    case RuntimeWalkStatus::schemaUnavailable:
        return CodecStatus::needsRuntimeSchema;
    case RuntimeWalkStatus::unsupportedField:
    case RuntimeWalkStatus::missingValue:
        return CodecStatus::unsupportedField;
    case RuntimeWalkStatus::unsafeCount:
        return CodecStatus::unsafeCount;
    case RuntimeWalkStatus::outputTooSmall:
        return CodecStatus::outputTooSmall;
    }
    return CodecStatus::malformed;
}

/** Maps one non-reflection layout to its explicit refusal status. */
CodecStatus layout_status(LayoutKind layout) noexcept {
    switch (layout) {
    case LayoutKind::packageSelected:
        return CodecStatus::needsPackageSchema;
    case LayoutKind::runtimeSelected:
        return CodecStatus::needsRuntimeSchema;
    case LayoutKind::reflectionWithRawTail:
        return CodecStatus::mixedRawTail;
    case LayoutKind::authored:
        return CodecStatus::unsupportedField;
    case LayoutKind::absent:
        return CodecStatus::noDefinition;
    case LayoutKind::reflection:
        return CodecStatus::complete;
    }
    return CodecStatus::malformed;
}

/** Accepts the one-field runtime-selected message root grammar. */
bool runtime_selected_root(const MessageDescriptor& message) noexcept {
    const std::span<const FieldDescriptor> root = fields(message);
    return message.layout == LayoutKind::runtimeSelected && root.size() == 1
           && root.front().typeCode == 34;
}

/** Finds or creates one exact field occurrence in an authored packet draft. */
DraftValue* edit(PacketDraft& draft, std::uint16_t fieldIndex, std::uint32_t element) noexcept {
    return edit(draft, fieldIndex, element, ValueRole::scalar);
}

/** Finds or creates one structural role on an outer field occurrence. */
DraftValue*
edit(PacketDraft& draft, std::uint16_t fieldIndex, std::uint32_t element, ValueRole role) noexcept {
    for (std::size_t index = 0; index < draft.valueCount; ++index) {
        DraftValue& value = draft.values[index];
        if (value.fieldIndex == fieldIndex && value.element == element
            && value.selectedSchemaHandle == kAbsentUnsigned && value.role == role) {
            return &value;
        }
    }
    if (draft.valueCount == draft.values.size()) {
        return nullptr;
    }
    DraftValue& value = draft.values[draft.valueCount++];
    value = {};
    value.fieldIndex = fieldIndex;
    value.element = element;
    value.role = role;
    value.assigned = true;
    return &value;
}

/** Finds or creates one exact field occurrence inside a selected schema. */
DraftValue* edit_selected(PacketDraft& draft,
                          std::uint16_t fieldIndex,
                          std::uint32_t element,
                          std::uint32_t schemaHandle,
                          std::uint32_t schemaRow,
                          std::uint32_t fieldRow,
                          std::uint32_t occurrence) noexcept {
    for (std::size_t index = 0; index < draft.valueCount; ++index) {
        DraftValue& value = draft.values[index];
        if (value.fieldIndex == fieldIndex && value.element == element
            && value.selectedSchemaHandle == schemaHandle && value.selectedSchemaRow == schemaRow
            && value.selectedFieldRow == fieldRow && value.selectedOccurrence == occurrence
            && value.role == ValueRole::scalar) {
            return &value;
        }
    }
    if (draft.valueCount == draft.values.size()) {
        return nullptr;
    }
    DraftValue& value = draft.values[draft.valueCount++];
    value = {};
    value.fieldIndex = fieldIndex;
    value.element = element;
    value.selectedSchemaHandle = schemaHandle;
    value.selectedSchemaRow = schemaRow;
    value.selectedFieldRow = fieldRow;
    value.selectedOccurrence = occurrence;
    value.assigned = true;
    return &value;
}

/** Returns the operator-facing explanation for one codec boundary or result. */
const char* codec_status_name(CodecStatus status) noexcept {
    switch (status) {
    case CodecStatus::complete:
        return "complete";
    case CodecStatus::completeWithPadding:
        return "complete + byte padding";
    case CodecStatus::needsPackageSchema:
        return "needs package-selected schema";
    case CodecStatus::needsRuntimeSchema:
        return "needs runtime-selected schema";
    case CodecStatus::mixedRawTail:
        return "reflection + raw tail";
    case CodecStatus::noDefinition:
        return "no wire definition";
    case CodecStatus::unsupportedField:
        return "field codec unavailable";
    case CodecStatus::unsafeCount:
        return "count exceeds native capacity";
    case CodecStatus::malformed:
        return "malformed or trailing data";
    case CodecStatus::outputTooSmall:
        return "output buffer too small";
    }
    return "unknown";
}

} // namespace sunrise::middleware::bap::activity_message::wire_schema
