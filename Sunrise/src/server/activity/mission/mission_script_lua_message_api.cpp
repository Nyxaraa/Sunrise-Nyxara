#include <array>
#include <string_view>

#include "mission_script_lua_event_internal.h"
#include "mission_script_lua_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {

/** Maps one client-message observation status to its Lua spelling. */
[[nodiscard]] const char*
lua_client_message_status_name(host::ClientMessageStatus status) noexcept {
    using Status = host::ClientMessageStatus;
    switch (status) {
    case Status::unclassified:
        return "unclassified";
    case Status::decoded:
        return "decoded";
    case Status::decodedPartial:
        return "decoded_partial";
    case Status::prefixOnly:
        return "prefix_only";
    case Status::opaque:
        return "opaque";
    case Status::outerDecoded:
        return "outer_decoded";
    case Status::prepared:
        return "prepared";
    case Status::prepareRefused:
        return "prepare_refused";
    case Status::malformed:
        return "malformed";
    case Status::quarantined:
        return "quarantined";
    }
    return "unknown";
}

/** Tests whether the running callback's typed or metadata-only event is this catalog message. */
[[nodiscard]] int message_matches(lua_State* state) {
    const auto* const handle =
        static_cast<const MessageHandle*>(luaL_checkudata(state, 1, kMessageMetatable));
    // Named arguments this call accepts. Any other key is refused.
    static constexpr std::array<std::string_view, 1> kDeclared{"event"};
    refuse_unknown_arguments(state, kDeclared);
    static_cast<void>(push_argument(state, "event"));
    const host::Event& event = check_event(state, lua_gettop(state));
    ActivityMessageDefinition definition{};
    namespace communication = middleware::bap::activity_message::wire_schema::communication;
    communication::ActivityCommunicationRoute route{};
    if (!current_message(state, *handle, definition) || !executable_message_route(definition, route)
        || receive_api_name(route) == std::string_view{"none"}) {
        return luaL_error(state, "activity message receive route is unavailable");
    }
    if (route.ingressClass == communication::IngressClass::nativeMetadataOnly
        || route.ingressClass == communication::IngressClass::nativeParsed) {
        lua_pushboolean(state,
                        event.kind == host::EventKind::clientMessageReceived
                                && event.clientMessageType == definition.messageId
                            ? 1
                            : 0);
        return 1;
    }
    const std::string_view eventName = event_kind_name(state, event.kind);
    bool matches = false;
    for (std::size_t index = 0; !eventName.empty() && index < definition.ingressSurfaceCount;
         ++index) {
        matches = matches || definition.ingressSurfaces[index].luaName == eventName;
    }
    lua_pushboolean(state, matches ? 1 : 0);
    return 1;
}

} // namespace sunrise::server::activity::mission::lua_vm::detail
