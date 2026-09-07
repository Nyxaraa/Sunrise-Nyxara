#include "mission_script_message_catalog.h"

#include <limits>

#include "../../../middleware/bap/activity_message/wire_schema/activity_communication_route.h"
#include "../../../middleware/bap/activity_message/wire_schema/activity_wire_schema.h"
#include "../../../state/activity_sdk/runtime.h"

namespace sunrise::server::activity::mission::message_catalog {
namespace {

namespace sdk = state::activity_sdk;
namespace format = state::activity_sdk::format;
namespace wire = middleware::bap::activity_message::wire_schema;
namespace communication = wire::communication;

[[nodiscard]] const sdk::BoundView* context_view(const void* context) noexcept {
    return static_cast<const sdk::BoundView*>(context);
}

/** The message schema is server-owned; the SDK view only scopes the Lua handle lifetime. */
[[nodiscard]] bool valid_view(const sdk::BoundView* view) noexcept {
    return view != nullptr && view->catalog != nullptr && sdk::bound_activity(*view) != nullptr
           && sdk::bound_scenario(*view) != nullptr;
}

/** @return The value as an unsigned row, or the absent index when it is negative. */
[[nodiscard]] constexpr std::uint32_t unsigned_or_absent(std::int64_t value) noexcept {
    return value < 0 ? format::kAbsentIndex : static_cast<std::uint32_t>(value);
}

/** Fills one message definition from its descriptor and its communication route. */
[[nodiscard]] bool copy_message(const sdk::BoundView& view,
                                const wire::MessageDescriptor& message,
                                std::uint32_t localRow,
                                lua_vm::ActivityMessageDefinition& output) noexcept {
    output = {};
    const communication::ActivityCommunicationRoute* const route =
        communication::find_route(message.id);
    if (!valid_view(&view) || localRow != message.id + 1U || route == nullptr) {
        return false;
    }

    output.name = message.name;
    output.ingressAdapter = communication::stable_name(route->ingressAdapter);
    output.ingressAdapterPath = communication::ingress_adapter_path(route->ingressAdapter);
    output.ingressClass = communication::stable_name(route->ingressClass);
    output.egressAdapter = communication::stable_name(route->egressAdapter);
    output.egressAdapterPath = communication::egress_adapter_path(route->egressAdapter);
    output.egressClass = communication::stable_name(route->egressClass);
    output.outputCodec = communication::stable_name(route->outputCodec);
    output.outputCodecPath = communication::output_codec_path(route->outputCodec);
    output.stateOwner = communication::stable_name(route->stateOwner);
    output.stateOwnerPath = communication::state_owner_path(route->stateOwner);
    output.luaExposure = communication::stable_name(route->luaExposure);
    output.ingressDelivery = communication::stable_name(route->ingressDelivery);
    output.egressDelivery = communication::stable_name(route->egressDelivery);
    output.lateJoinHandoff = communication::stable_name(route->lateJoinHandoff);
    output.ingressStatus = communication::stable_name(route->ingressStatus);
    output.egressStatus = communication::stable_name(route->egressStatus);

    output.localRow = localRow;
    output.messageId = message.id;
    output.direction = static_cast<std::uint32_t>(message.direction);
    output.coverage = static_cast<std::uint32_t>(message.coverage);
    output.definitionHandle = message.definitionHandle;
    output.callForm = static_cast<std::uint32_t>(message.callForm);
    output.definitionState = static_cast<std::uint32_t>(message.definitionState);
    output.definitionStructSize = message.definitionStructSize;
    output.wireMinBits = unsigned_or_absent(message.wireMinBits);
    output.wireMaxBits = unsigned_or_absent(message.wireMaxBits);
    output.fieldCount = message.fieldCount;
    output.namedFieldCount = message.namedFieldCount;
    output.graphFieldCount = message.graphFieldCount;
    output.authoredFieldCount = message.authoredFieldCount;
    output.typedLuaSurfaceCount = route->typedLuaSurfaceCount;
    output.communicationFlags = format::kActivityCommunicationDataOnly;
    output.flags = format::kActivityMessageDataOnly;
    output.executableRoute = true;
    return !output.name.empty();
}

[[nodiscard]] bool resolve_row(const void* context,
                               std::uint32_t localRow,
                               lua_vm::ActivityMessageDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    const auto messages = wire::messages();
    return valid_view(view) && localRow != 0 && localRow <= messages.size()
           && copy_message(*view, messages[localRow - 1U], localRow, output);
}

[[nodiscard]] bool resolve_id(const void* context,
                              std::uint32_t messageId,
                              lua_vm::ActivityMessageDefinition& output) noexcept {
    return messageId != (std::numeric_limits<std::uint32_t>::max)()
           && resolve_row(context, messageId + 1U, output) && output.messageId == messageId;
}

/** Finds the message with this name and fills its definition. */
[[nodiscard]] bool resolve_name(const void* context,
                                std::string_view name,
                                lua_vm::ActivityMessageDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    if (!valid_view(view) || name.empty()) {
        return false;
    }
    const auto messages = wire::messages();
    for (std::size_t index = 0; index < messages.size(); ++index) {
        if (messages[index].name == name) {
            return copy_message(
                *view, messages[index], static_cast<std::uint32_t>(index + 1U), output);
        }
    }
    return false;
}

/** Packs one field descriptor's layout and exposure bits into the catalog flag word. */
[[nodiscard]] std::uint32_t field_flags(const wire::FieldDescriptor& field) noexcept {
    std::uint32_t output = format::kActivityMessageFieldDataOnly;
    output |= field.presenceBit ? format::kActivityMessageFieldPresenceBit : 0U;
    output |= field.coined ? format::kActivityMessageFieldCoinedName : 0U;
    output |= field.documented ? format::kActivityMessageFieldDocumentedRow : 0U;
    output |= field.repeatedBlock ? format::kActivityMessageFieldRepeatedBlock : 0U;
    if (field.exposure == wire::FieldExposure::operatorValue) {
        output |= format::kActivityMessageFieldOperatorValue;
    } else if (field.exposure == wire::FieldExposure::provisionalValue) {
        output |= format::kActivityMessageFieldProvisionalValue;
    }
    return output;
}

/** Fills one field definition from its message's descriptor table by local row. */
[[nodiscard]] bool copy_field(const wire::MessageDescriptor& message,
                              std::uint32_t messageRow,
                              std::uint32_t localRow,
                              lua_vm::ActivityMessageFieldDefinition& output) noexcept {
    output = {};
    const auto fields = wire::all_fields();
    if (messageRow != message.id + 1U || localRow == 0 || localRow > message.fieldCount
        || message.firstField > fields.size()
        || message.fieldCount > fields.size() - message.firstField) {
        return false;
    }
    const std::size_t globalIndex = message.firstField + localRow - 1U;
    const wire::FieldDescriptor& field = fields[globalIndex];
    output.path = field.path;
    output.name = field.name;
    output.type = field.type;
    output.localRow = localRow;
    output.globalRow = static_cast<std::uint32_t>(globalIndex + 1U);
    output.messageRow = messageRow;
    output.ordinal = localRow - 1U;
    output.source = static_cast<std::uint32_t>(field.sourceKind);
    output.structOffset = unsigned_or_absent(field.structOffset);
    output.structOffsetAbs = unsigned_or_absent(field.absoluteStructOffset);
    output.typeCode = field.typeCodePresent ? field.typeCode : format::kAbsentIndex;
    output.bias = field.biasPresent ? field.bias : format::kAbsentSignedValue;
    output.bits = unsigned_or_absent(field.bits);
    output.bitsMin = unsigned_or_absent(field.bitsMin);
    output.bitsMax = unsigned_or_absent(field.bitsMax);
    output.widthOrCountOffset = unsigned_or_absent(field.widthOrCountOffset);
    output.repeat = field.repeat;
    output.nestedHandle = field.nestedHandle;
    output.ownerHandle = field.ownerHandle;
    output.depth = field.depth;
    output.flags = field_flags(field);
    output.confidence = static_cast<std::uint32_t>(field.confidence);
    return !output.path.empty();
}

[[nodiscard]] bool resolve_field_row(const void* context,
                                     std::uint32_t messageRow,
                                     std::uint32_t localRow,
                                     lua_vm::ActivityMessageFieldDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    const auto messages = wire::messages();
    return valid_view(view) && messageRow != 0 && messageRow <= messages.size()
           && copy_field(messages[messageRow - 1U], messageRow, localRow, output);
}

/** Fills the field definition at one global field index within a message. */
[[nodiscard]] bool resolve_field_index(const void* context,
                                       std::uint32_t messageId,
                                       std::uint32_t globalFieldIndex,
                                       lua_vm::ActivityMessageFieldDefinition& output) noexcept {
    output = {};
    const sdk::BoundView* const view = context_view(context);
    const auto messages = wire::messages();
    if (!valid_view(view) || messageId >= messages.size()) {
        return false;
    }
    const wire::MessageDescriptor& message = messages[messageId];
    if (globalFieldIndex < message.firstField
        || globalFieldIndex - message.firstField >= message.fieldCount) {
        return false;
    }
    return copy_field(message, messageId + 1U, globalFieldIndex - message.firstField + 1U, output);
}

[[nodiscard]] std::size_t count(const void* context) noexcept {
    return valid_view(context_view(context)) ? wire::messages().size() : 0;
}

} // namespace

void attach(lua_vm::DefinitionApi& output) noexcept {
    output.resolveActivityMessageRow = &resolve_row;
    output.resolveActivityMessageId = &resolve_id;
    output.resolveActivityMessageName = &resolve_name;
    output.resolveActivityMessageFieldRow = &resolve_field_row;
    output.resolveActivityMessageFieldIndex = &resolve_field_index;
    output.activityMessageCount = &count;
}

} // namespace sunrise::server::activity::mission::message_catalog
