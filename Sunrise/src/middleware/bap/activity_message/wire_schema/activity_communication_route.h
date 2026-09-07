#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "activity_wire_schema.h"

namespace sunrise::middleware::bap::activity_message::wire_schema::communication {

/** One communications row exists for each generated message row. */
inline constexpr std::size_t kRouteCount = kMessageCount;

/** Runtime adapter that accepts one client message. */
enum class IngressAdapter : std::uint8_t {
    none,
    joinRequestStateJoin,
    senseUpdateHostSense,
    routeMisuseReceipt,
    startActivityOptionalStateRefresh,
    reservationRequest,
    reservationRelease,
    peerLeave,
    clientKeepalive,
    incidentHostIncident,
    stateRefreshMembership,
    entitySlotRequestStateSlots,
    entitySlotsStateSlots,
    clientAuthoritativeDataMembership,
    clientIdentityMembership,
    authorityAbandon,
    authorityRequestPurge,
    authorityResetAcknowledgement,
    authorityQueryAnswer,
    authorityAbdicate,
    debugCommandQuarantine,
    connectivityFailure,
    membershipAcknowledgement,
    heartbeat,
    opaqueScalar,
    lagSwitch,
    connectionQuality,
    migration,
    highWater,
    patchEpochStateEpoch,
};

/** How far one client message may enter the server mission API. */
enum class IngressClass : std::uint8_t {
    notClientSent,
    acceptedJoinRecordOnly,
    nativeMetadataOnly,
    nativeParsed,
    typedOnly,
    typedPostCommitOnly,
};

/** Server path that stages one client-handled message. */
enum class EgressAdapter : std::uint8_t {
    none,
    appendEntitySlotNotification,
    appendGlobalStateNotification,
    appendWorldGlobalsNotification,
    appendJoinNotifications,
    appendRosterNotification,
    appendMembershipNotification,
    appendIncidentNotification,
    appendJoinNotificationsMessage54,
    appendAuthorityResetNotification,
    appendAuthorityQueryNotification,
};

/** Current server-output coverage for one client-handled message. */
enum class EgressClass : std::uint8_t {
    notServerOutputDirection,
    clientHandlesNoServerRoute,
    routedNoLuaAction,
    routedProtocolOwned,
    routedTypedLuaAction,
};

/** Dedicated body codec, independent of whether a server route calls it. */
enum class OutputCodec : std::uint8_t {
    none,
    encodeEntitySlots,
    encodeGlobalActivityState,
    encodeWorldGlobalsState,
    encodeSensorMessage,
    encodeStartActivityHostResponse,
    encodeJoinResult,
    encodeSensorAuthUpdate,
    encodeReplicateMembership,
    writeIncident,
    encodeReplicationEpoch,
    encodeClaimAuthority,
    encodePurgeAuthority,
    encodeResetAuthorityMask,
    encodeQueryAuthorityMask,
    encodeReservationsFailed,
    encodePerfRequestKill,
    encodePerfRequestReflect,
    encodeScriptState,
    encodeScriptEvent,
    encodeConnectivityFailure,
    encodeBubbleHostState,
};

/** Runtime component that owns the retained mutation or typed mission queue. */
enum class StateOwner : std::uint8_t {
    none,
    activityHost,
    activityMembershipState,
    activityEntitySlotsState,
    activityMessageTransaction,
    activityPatchEpochConnection,
    activityAuthorityResetConnection,
    activityAuthorityConnection,
};

/** Lua view attached to one message id. */
enum class LuaExposure : std::uint8_t {
    sdkMetadataOnly,
    messageMetadataOnly,
    typedEvent,
    typedAction,
};

/** Ordered ingress delivery after the wire adapter accepts a message. */
enum class IngressDeliveryPolicy : std::uint8_t {
    none,
    joinedRecordOnly,
    protocolHostInput,
    typedHostInput,
    postCommitTypedHostInput,
};

/** Outbound delivery owned by a server route. */
enum class EgressDeliveryPolicy : std::uint8_t {
    none,
    protocolNotification,
    typedAuthStaging,
};

/** Current late-join and host-handoff treatment. */
enum class LateJoinHandoffPolicy : std::uint8_t {
    notDeclared,
    protocolLifecycleOwned,
    missionStatePublicationUnresolved,
    noServerRoute,
};

/** Whether the direction has a server route and a bounded semantic adapter. */
enum class RouteStatus : std::uint8_t {
    notApplicable,
    resolved,
    noServerRoute,
    routedActionSemanticsUnresolved,
};

/** One bounded communications projection for an activity-message id. */
struct ActivityCommunicationRoute final {
    std::uint32_t messageId{};
    IngressAdapter ingressAdapter{IngressAdapter::none};
    IngressClass ingressClass{IngressClass::notClientSent};
    EgressAdapter egressAdapter{EgressAdapter::none};
    EgressClass egressClass{EgressClass::notServerOutputDirection};
    OutputCodec outputCodec{OutputCodec::none};
    StateOwner stateOwner{StateOwner::none};
    LuaExposure luaExposure{LuaExposure::sdkMetadataOnly};
    IngressDeliveryPolicy ingressDelivery{IngressDeliveryPolicy::none};
    EgressDeliveryPolicy egressDelivery{EgressDeliveryPolicy::none};
    LateJoinHandoffPolicy lateJoinHandoff{LateJoinHandoffPolicy::notDeclared};
    RouteStatus ingressStatus{RouteStatus::notApplicable};
    RouteStatus egressStatus{RouteStatus::notApplicable};
    std::uint8_t typedLuaSurfaceCount{};
};

/** One generated message identity used to authorize compiled communication code. */
struct MessageIdentity final {
    std::uint32_t messageId{};
    std::string_view name{};
};

/** Data-only route identity loaded from the generated SDK pack. */
struct RouteIdentity final {
    std::uint32_t messageIndex{};
    std::uint32_t messageId{};
    std::string_view ingressAdapter{};
    std::string_view ingressAdapterPath{};
    std::string_view ingressClass{};
    std::string_view egressAdapter{};
    std::string_view egressAdapterPath{};
    std::string_view egressClass{};
    std::string_view outputCodec{};
    std::string_view outputCodecPath{};
    std::string_view stateOwner{};
    std::string_view stateOwnerPath{};
    std::string_view luaExposure{};
    std::string_view ingressDelivery{};
    std::string_view egressDelivery{};
    std::string_view lateJoinHandoff{};
    std::string_view ingressStatus{};
    std::string_view egressStatus{};
    std::uint32_t typedLuaSurfaceCount{};
    std::uint32_t flags{};
};

/** Generated route rows carry this flag and no executable pointer. */
inline constexpr std::uint32_t kRouteIdentityDataOnly = 0x1U;

/** Atomic result of building one generated executable registry. */
enum class ExecutableRegistryStatus : std::uint8_t {
    empty,
    ready,
    wrongExtent,
    messageIdentityMismatch,
    routeIdentityMismatch,
};

/** No message row failed while building the registry. */
inline constexpr std::uint32_t kNoFailedMessageId = 0xFFFFFFFFU;

/** One exact message identity and its generated executable route value. */
struct ExecutableRoute final {
    std::uint32_t messageId{};
    std::string_view messageName{};
    ActivityCommunicationRoute route{};
};

/** Fixed immutable route values built from one validated generated SDK catalog. */
class ExecutableRegistry final {
public:
    /** @return Every message row only after the complete registry binds. */
    [[nodiscard]] std::span<const ExecutableRoute> entries() const noexcept;
    /** @return An authorized generated route, or null while unavailable or for an unknown id. */
    [[nodiscard]] const ActivityCommunicationRoute* find(std::uint32_t messageId) const noexcept;
    /** @return An authorized route only when the caller also supplies its exact generated name. */
    [[nodiscard]] const ActivityCommunicationRoute*
    find(std::uint32_t messageId, std::string_view messageName) const noexcept;
    /** @return Full row count for a ready registry, otherwise zero. */
    [[nodiscard]] std::size_t matched_count() const noexcept;
    /** @return True after every generated message and route row bound atomically. */
    [[nodiscard]] bool ready() const noexcept;
    /** @return Atomic build state, including the first exact refusal class. */
    [[nodiscard]] ExecutableRegistryStatus status() const noexcept;
    /** @return First refused message id, or kNoFailedMessageId when no row was attempted. */
    [[nodiscard]] std::uint32_t failed_message_id() const noexcept;

private:
    friend bool build_executable_registry(std::span<const MessageIdentity> messages,
                                          std::span<const RouteIdentity> routeIdentities,
                                          ExecutableRegistry& output) noexcept;

    std::array<ExecutableRoute, kRouteCount> entries_{};
    std::size_t matchedCount_{};
    ExecutableRegistryStatus status_{ExecutableRegistryStatus::empty};
    std::uint32_t failedMessageId_{kNoFailedMessageId};
};

/** @return Generated route seed rows used only by native SDK generation and focused fixtures. */
[[nodiscard]] std::span<const ActivityCommunicationRoute> routes() noexcept;

/** @return Generated SDK seed route for one message id, or null outside 0..58. */
[[nodiscard]] const ActivityCommunicationRoute* find_route(std::uint32_t messageId) noexcept;

/**
 * Binds data-only SDK identities through generated finite domains without serialized pointers.
 * Any extent, message, name, path, direction, or policy mismatch denies the whole registry.
 */
[[nodiscard]] bool build_executable_registry(std::span<const MessageIdentity> messages,
                                             std::span<const RouteIdentity> routeIdentities,
                                             ExecutableRegistry& output) noexcept;

/** @return Schema identity for the data-only SDK and Lua projection. */
[[nodiscard]] std::string_view sdk_projection_schema() noexcept;

/** @return Stable SDK spelling for one checked communication enum, or an empty view. */
[[nodiscard]] std::string_view stable_name(IngressAdapter value) noexcept;
[[nodiscard]] std::string_view stable_name(IngressClass value) noexcept;
[[nodiscard]] std::string_view stable_name(EgressAdapter value) noexcept;
[[nodiscard]] std::string_view stable_name(EgressClass value) noexcept;
[[nodiscard]] std::string_view stable_name(OutputCodec value) noexcept;
[[nodiscard]] std::string_view stable_name(StateOwner value) noexcept;
[[nodiscard]] std::string_view stable_name(LuaExposure value) noexcept;
[[nodiscard]] std::string_view stable_name(IngressDeliveryPolicy value) noexcept;
[[nodiscard]] std::string_view stable_name(EgressDeliveryPolicy value) noexcept;
[[nodiscard]] std::string_view stable_name(LateJoinHandoffPolicy value) noexcept;
[[nodiscard]] std::string_view stable_name(RouteStatus value) noexcept;

/** @return True only for typed ingress that cannot use the protocol metadata event. */
[[nodiscard]] constexpr bool
message_lua_input_denied(const ActivityCommunicationRoute& route) noexcept {
    return route.ingressClass == IngressClass::typedOnly
           || route.ingressClass == IngressClass::typedPostCommitOnly;
}

/** @return Audited C++ path for an ingress adapter, or an empty view. */
[[nodiscard]] std::string_view ingress_adapter_path(IngressAdapter adapter) noexcept;

/** @return Audited C++ path for a server output route, or an empty view. */
[[nodiscard]] std::string_view egress_adapter_path(EgressAdapter adapter) noexcept;

/** @return Audited C++ path for an output body codec, or an empty view. */
[[nodiscard]] std::string_view output_codec_path(OutputCodec codec) noexcept;

/** @return C++ component path for a retained state owner, or an empty view. */
[[nodiscard]] std::string_view state_owner_path(StateOwner owner) noexcept;

} // namespace sunrise::middleware::bap::activity_message::wire_schema::communication
