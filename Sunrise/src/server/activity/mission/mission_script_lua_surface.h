#pragma once

#include <string_view>

#include "../../../middleware/bap/activity_message/wire_schema/activity_communication_route.h"
#include "../../../state/activity_sdk/runtime.h"
#include "mission_script_vm_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

/** Resolves one message through the server-owned executable route table. */
[[nodiscard]] inline bool executable_message_route(
    const ActivityMessageDefinition& definition,
    middleware::bap::activity_message::wire_schema::communication::ActivityCommunicationRoute&
        output) noexcept {
    output = {};
    return state::activity_sdk::executable_communication_route(definition.messageId, output);
}

/** @return The Lua receive surface a resolved ingress class exposes, or "none". */
[[nodiscard]] inline const char* receive_api_name(
    const middleware::bap::activity_message::wire_schema::communication::ActivityCommunicationRoute&
        route) noexcept {
    namespace communication = middleware::bap::activity_message::wire_schema::communication;
    if (route.ingressStatus != communication::RouteStatus::resolved) {
        return "none";
    }
    switch (route.ingressClass) {
    case communication::IngressClass::nativeMetadataOnly:
    case communication::IngressClass::nativeParsed:
        return "message_event";
    case communication::IngressClass::typedOnly:
    case communication::IngressClass::typedPostCommitOnly:
        return "typed_event";
    case communication::IngressClass::notClientSent:
    case communication::IngressClass::acceptedJoinRecordOnly:
        return "none";
    }
    return "none";
}

/** @return True only for a routed typed Lua action that stages through Auth. */
[[nodiscard]] inline bool send_api_available(
    const middleware::bap::activity_message::wire_schema::communication::ActivityCommunicationRoute&
        route) noexcept {
    namespace communication = middleware::bap::activity_message::wire_schema::communication;
    return route.egressStatus == communication::RouteStatus::resolved
           && route.egressClass == communication::EgressClass::routedTypedLuaAction
           && route.egressDelivery == communication::EgressDeliveryPolicy::typedAuthStaging;
}

} // namespace sunrise::server::activity::mission::lua_vm::detail
