#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "../../../middleware/bap/service_catalog.h"
#include "mission_script_lua_bap_internal.h"

namespace sunrise::server::activity::mission::lua_vm::detail {
namespace {

namespace bap_catalog = middleware::bap::service_catalog;

void push_bap_service(lua_State* state, std::uint32_t row) {
    push_handle(state, kBapServiceMetatable, BapServiceHandle{row});
}

/** Converts an outer BAP id without admitting an Activity Message id by association. */
[[nodiscard]] std::uint16_t checked_bap_service_id(lua_State* state, int index) {
    const lua_Integer value = luaL_checkinteger(state, index);
    if (value < 0
        || static_cast<std::uint64_t>(value) > (std::numeric_limits<std::uint16_t>::max)()) {
        luaL_argerror(state, index, "BAP service id is outside u16");
    }
    return static_cast<std::uint16_t>(value);
}

/** Resolves one static catalog pointer back to its one-based Lua row. */
[[nodiscard]] bool bap_service_row(const bap_catalog::Service* service,
                                   std::uint32_t& output) noexcept {
    output = 0;
    const std::span<const bap_catalog::Service> rows = bap_catalog::services();
    for (std::size_t row = 0; row < rows.size(); ++row) {
        if (service == &rows[row]) {
            output = static_cast<std::uint32_t>(row);
            return true;
        }
    }
    return false;
}

[[nodiscard]] int bap_service_collection_at(lua_State* state) {
    static_cast<void>(luaL_checkudata(state, 1, kBapServiceCollectionMetatable));
    const lua_Integer selected = luaL_checkinteger(state, 2);
    if (selected <= 0 || static_cast<std::uint64_t>(selected) > bap_catalog::services().size()) {
        return luaL_error(state, "BAP service row is unavailable");
    }
    push_bap_service(state, static_cast<std::uint32_t>(selected - 1));
    return 1;
}

template <const bap_catalog::Service* (*Find)(std::uint16_t) noexcept>
[[nodiscard]] int bap_service_collection_find(lua_State* state) {
    static_cast<void>(luaL_checkudata(state, 1, kBapServiceCollectionMetatable));
    std::uint32_t row = 0;
    if (!bap_service_row(Find(checked_bap_service_id(state, 2)), row)) {
        return luaL_error(state, "BAP service id is unavailable for this role");
    }
    push_bap_service(state, row);
    return 1;
}

void push_optional_bap_service_id(lua_State* state, std::uint16_t serviceId) {
    if (serviceId == bap_catalog::kAbsentServiceId) {
        lua_pushnil(state);
    } else {
        lua_pushinteger(state, serviceId);
    }
}

void push_optional_bap_extent(lua_State* state, std::size_t value) {
    if (value == bap_catalog::kAbsentBodyExtent) {
        lua_pushnil(state);
    } else {
        lua_pushinteger(state, static_cast<lua_Integer>(value));
    }
}

} // namespace

void push_bap_service_collection(lua_State* state) {
    push_handle(state, kBapServiceCollectionMetatable, BapServiceCollectionHandle{});
}
/** Reads a row, a lookup by service id, or the collection count. */
[[nodiscard]] int bap_service_collection_index(lua_State* state) {
    static_cast<void>(luaL_checkudata(state, 1, kBapServiceCollectionMetatable));
    const std::string_view key = lua_string_view(state, 2);
    if (key == "count" || key == "row_count") {
        lua_pushinteger(state, static_cast<lua_Integer>(bap_catalog::services().size()));
    } else if (key == "schema") {
        const std::string_view schema = bap_catalog::schema();
        lua_pushlstring(state, schema.data(), schema.size());
    } else if (key == "runtime_authority") {
        lua_pushliteral(state, "protocol");
    } else if (key == "query_only") {
        lua_pushboolean(state, 1);
    } else if (key == "script_sendable") {
        lua_pushboolean(state, 0);
    } else if (key == "raw_bodies") {
        lua_pushliteral(state, "not_exposed");
    } else if (key == "activity_message_namespace") {
        lua_pushliteral(state, "separate_inner_activity_message_id");
    } else if (key == "at") {
        lua_pushcfunction(state, &bap_service_collection_at);
    } else if (key == "by_request_id") {
        lua_pushcfunction(state, &bap_service_collection_find<&bap_catalog::find_request>);
    } else if (key == "by_response_id") {
        lua_pushcfunction(state, &bap_service_collection_find<&bap_catalog::find_response>);
    } else if (key == "by_notification_id") {
        lua_pushcfunction(state, &bap_service_collection_find<&bap_catalog::find_notification>);
    } else {
        lua_pushnil(state);
    }
    return 1;
}
/** Reads one service row's identity, acceptance policy, and frame extents. */
[[nodiscard]] int bap_service_index(lua_State* state) {
    const auto* const handle =
        static_cast<const BapServiceHandle*>(luaL_checkudata(state, 1, kBapServiceMetatable));
    if (handle->row >= bap_catalog::services().size()) {
        return luaL_error(state, "BAP service row is unavailable");
    }
    const bap_catalog::Service& service = bap_catalog::services()[handle->row];
    const std::string_view key = lua_string_view(state, 2);
    if (key == "row") {
        lua_pushinteger(state, static_cast<lua_Integer>(handle->row) + 1);
    } else if (key == "id") {
        lua_pushlstring(state, service.id.data(), service.id.size());
    } else if (key == "service_id") {
        lua_pushinteger(state, service.serviceId);
    } else if (key == "request_service_id") {
        push_optional_bap_service_id(state,
                                     service.role == bap_catalog::ServiceRole::request
                                         ? service.serviceId
                                         : bap_catalog::kAbsentServiceId);
    } else if (key == "notification_service_id") {
        push_optional_bap_service_id(state,
                                     service.role == bap_catalog::ServiceRole::notification
                                         ? service.serviceId
                                         : bap_catalog::kAbsentServiceId);
    } else if (key == "response_service_id") {
        push_optional_bap_service_id(state, service.responseServiceId);
    } else if (key == "service_role") {
        const std::string_view value = bap_catalog::stable_name(service.role);
        lua_pushlstring(state, value.data(), value.size());
    } else if (key == "security") {
        const std::string_view value = bap_catalog::stable_name(service.security);
        lua_pushlstring(state, value.data(), value.size());
    } else if (key == "response_mode") {
        const std::string_view value = bap_catalog::stable_name(service.responseMode);
        lua_pushlstring(state, value.data(), value.size());
    } else if (key == "acceptance_policy") {
        const std::string_view value = bap_catalog::stable_name(service.acceptancePolicy);
        lua_pushlstring(state, value.data(), value.size());
    } else if (key == "body_shape") {
        const std::string_view value = bap_catalog::stable_name(service.bodyShape);
        lua_pushlstring(state, value.data(), value.size());
    } else if (key == "response_body_shape") {
        const std::string_view value = bap_catalog::stable_name(service.responseBodyShape);
        lua_pushlstring(state, value.data(), value.size());
    } else if (key == "body_exact_bytes") {
        push_optional_bap_extent(state,
                                 service.bodyShape == bap_catalog::BodyShape::fixed
                                     ? service.bodyExtent
                                     : bap_catalog::kAbsentBodyExtent);
    } else if (key == "body_prefix_bytes") {
        push_optional_bap_extent(state,
                                 service.bodyShape == bap_catalog::BodyShape::boundedTypedPayload
                                     ? service.bodyExtent
                                     : bap_catalog::kAbsentBodyExtent);
    } else if (key == "alternate_body_prefix_bytes") {
        push_optional_bap_extent(state,
                                 service.bodyShape == bap_catalog::BodyShape::boundedTypedPayload
                                     ? service.alternateBodyExtent
                                     : bap_catalog::kAbsentBodyExtent);
    } else if (key == "complete_encrypted_frame_overhead_bytes") {
        push_optional_bap_extent(state, service.completeEncryptedFrameOverhead);
    } else if (key == "complete_encrypted_frame_max_bytes") {
        push_optional_bap_extent(state, service.completeEncryptedFrameMaximum);
    } else if (key == "response_body_exact_bytes") {
        push_optional_bap_extent(state,
                                 service.responseBodyShape == bap_catalog::BodyShape::fixed
                                     ? service.responseBodyExtent
                                     : bap_catalog::kAbsentBodyExtent);
    } else if (key == "typed_payload_limit") {
        if (service.typedPayloadLimit == 0) {
            lua_pushnil(state);
        } else {
            lua_pushinteger(state, static_cast<lua_Integer>(service.typedPayloadLimit));
        }
    } else if (key == "body_codec") {
        lua_pushlstring(state, service.bodyCodec.data(), service.bodyCodec.size());
    } else if (key == "body_codec_path") {
        lua_pushlstring(state, service.bodyCodecPath.data(), service.bodyCodecPath.size());
    } else if (key == "response_body_codec") {
        lua_pushlstring(state, service.responseBodyCodec.data(), service.responseBodyCodec.size());
    } else if (key == "response_body_codec_path") {
        if (service.responseBodyCodecPath.empty()) {
            lua_pushnil(state);
        } else {
            lua_pushlstring(
                state, service.responseBodyCodecPath.data(), service.responseBodyCodecPath.size());
        }
    } else if (key == "route") {
        lua_pushlstring(state, service.route.data(), service.route.size());
    } else if (key == "route_path") {
        lua_pushlstring(state, service.routePath.data(), service.routePath.size());
    } else if (key == "activity_message_envelope") {
        lua_pushboolean(state, service.activityMessageEnvelope ? 1 : 0);
    } else if (key == "inner_message_namespace") {
        lua_pushstring(state, service.activityMessageEnvelope ? "activity_message_id" : "none");
    } else if (key == "lifecycle_scope") {
        const std::string_view value = bap_catalog::stable_name(service.lifecycleScope);
        lua_pushlstring(state, value.data(), value.size());
    } else if (key == "service_id_namespace") {
        lua_pushliteral(state, "bap_service_id");
    } else if (key == "protocol_scope") {
        const std::string_view value = bap_catalog::stable_name(service.protocolScope);
        lua_pushlstring(state, value.data(), value.size());
    } else if (key == "runtime_authority") {
        lua_pushliteral(state, "protocol");
    } else if (key == "data_only" || key == "executable" || key == "query_only") {
        lua_pushboolean(state, 1);
    } else if (key == "script_sendable" || key == "raw_body_exposed") {
        lua_pushboolean(state, 0);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

void register_bap_metatables(lua_State* state) {
    register_metatable(state, kBapServiceCollectionMetatable, &bap_service_collection_index);
    register_metatable(state, kBapServiceMetatable, &bap_service_index);
}
} // namespace sunrise::server::activity::mission::lua_vm::detail
