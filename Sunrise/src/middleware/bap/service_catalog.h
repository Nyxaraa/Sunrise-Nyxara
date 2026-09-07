#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "../../client/network/consumer.h"
#include "activity_host/activity_host_response.h"
#include "activity_host_manager/request/activity_manager_request.h"
#include "activity_host_manager/response/activity_manager_response.h"
#include "activity_message/activity_message_notification_encoder.h"
#include "frame.h"

namespace sunrise::middleware::bap::service_catalog {

/** No paired response service exists for this row. */
inline constexpr std::uint16_t kAbsentServiceId = (std::numeric_limits<std::uint16_t>::max)();
/** No exact body or fixed-prefix extent is proved for this row. */
inline constexpr std::size_t kAbsentBodyExtent = (std::numeric_limits<std::size_t>::max)();
/** Fixed service-25 token and version request extent. */
inline constexpr std::size_t kServerHelloRequestBodySize = 36;
/** Fixed service-26 key-envelope extent. */
inline constexpr std::size_t kServerHelloResponseBodySize = 84;
/** Service-8 fixed fields before its typed Activity Message payload. */
inline constexpr std::size_t kActivityMessageRequestPrefixSize = 21;
/** Service-8 discriminator-2 prefix omits the four-byte peer-heard mask. */
inline constexpr std::size_t kCompactActivityMessageRequestPrefixSize = 17;
/** Service-9 fixed fields before its typed Activity Message payload. */
inline constexpr std::size_t kActivityMessageNotificationPrefixSize = 17;
/** Complete encrypted svc8 frame maximum, including every outer transport layer. */
inline constexpr std::size_t kActivityMessageRequestCompleteEncryptedFrameMaximum =
    client::network::kBapFrameCapacity;
/** Bytes around the largest typed svc8 payload in one complete encrypted frame. */
inline constexpr std::size_t kActivityMessageRequestCompleteEncryptedFrameOverhead =
    kActivityMessageRequestCompleteEncryptedFrameMaximum - activity_message::kMaximumPayloadSize;
/** Bytes around one typed svc9 payload in one complete encrypted frame. */
inline constexpr std::size_t kActivityMessageNotificationCompleteEncryptedFrameOverhead =
    kActivityMessageRequestCompleteEncryptedFrameOverhead - kActivityMessageRequestPrefixSize
    + kActivityMessageNotificationPrefixSize;
/** Complete encrypted svc9 frame maximum, including every outer transport layer. */
inline constexpr std::size_t kActivityMessageNotificationCompleteEncryptedFrameMaximum =
    kActivityMessageNotificationCompleteEncryptedFrameOverhead
    + activity_message::kMaximumPayloadSize;

/** How the primary service appears on the BAP transport. */
enum class ServiceRole : std::uint8_t {
    request,
    notification,
};

/** Whether the service body travels before or after secure-channel bootstrap. */
enum class Security : std::uint8_t {
    plaintext,
    encrypted,
};

/** Relationship between the primary service and any paired service. */
enum class ResponseMode : std::uint8_t {
    correlated,
    oneWay,
    notification,
};

/** Observable acceptance behavior of the live protocol route. */
enum class AcceptancePolicy : std::uint8_t {
    bestEffortStateFallbackReply,
    strictOneWayEnvelope,
    outboundOnly,
    thinReplyOnBodyRefusal,
    bodyIgnoredBootstrapReply,
    opaqueEcho,
    bodyIgnoredEmptyReply,
};

/** Exact body contract established by the executable codec. */
enum class BodyShape : std::uint8_t {
    none,
    fixed,
    boundedTypedPayload,
    opaque,
    opaqueEcho,
};

/** Protocol stage that owns this global service route. */
enum class ProtocolScope : std::uint8_t {
    activityHostAllocation,
    activityMessageIngress,
    activityMessageNotification,
    activityHostLookup,
    secureChannelBootstrap,
    channelStart,
    keepalive,
};

/** Lifecycle family owned by the protocol, or none for data-plane envelopes. */
enum class LifecycleScope : std::uint8_t {
    none,
    activityHost,
    secureChannel,
    transport,
};

/** One global protocol route. It is never activity-pack content or a script send surface. */
struct Service final {
    std::string_view id{};
    std::uint16_t serviceId{};
    std::uint16_t responseServiceId{kAbsentServiceId};
    ServiceRole role{ServiceRole::request};
    Security security{Security::encrypted};
    ResponseMode responseMode{ResponseMode::oneWay};
    BodyShape bodyShape{BodyShape::none};
    BodyShape responseBodyShape{BodyShape::none};
    std::size_t bodyExtent{kAbsentBodyExtent};
    std::size_t responseBodyExtent{kAbsentBodyExtent};
    std::size_t typedPayloadLimit{};
    std::string_view bodyCodec{};
    std::string_view bodyCodecPath{};
    std::string_view responseBodyCodec{};
    std::string_view responseBodyCodecPath{};
    std::string_view route{};
    std::string_view routePath{};
    bool activityMessageEnvelope{};
    ProtocolScope protocolScope{ProtocolScope::activityHostAllocation};
    LifecycleScope lifecycleScope{LifecycleScope::none};
    bool scriptSendable{};
    AcceptancePolicy acceptancePolicy{AcceptancePolicy::strictOneWayEnvelope};
    /** Optional second fixed prefix accepted by a bounded-payload decoder. */
    std::size_t alternateBodyExtent{kAbsentBodyExtent};
    /** Complete encrypted frame overhead, or absent when this row has no proved maximum. */
    std::size_t completeEncryptedFrameOverhead{kAbsentBodyExtent};
    /** Complete encrypted frame maximum, or absent when this row has no proved maximum. */
    std::size_t completeEncryptedFrameMaximum{kAbsentBodyExtent};
};

/** Every BAP service Sunrise answers. A service outside this table is refused. */
inline constexpr std::array<Service, 7> kServices{{
    Service{"bap.activity_host_manager.v1",
            static_cast<std::uint16_t>(RequestService::activityHostManager),
            static_cast<std::uint16_t>(ResponseService::activityHostManager),
            ServiceRole::request,
            Security::encrypted,
            ResponseMode::correlated,
            BodyShape::fixed,
            BodyShape::fixed,
            activity_host_manager::request::kRequestBodySize,
            activity_host_manager::response::kResponseBodySize,
            0,
            "activity_host_manager_request_7719",
            "middleware/bap/activity_host_manager/request/activity_manager_request.cpp",
            "activity_host_manager_response_137",
            "middleware/bap/activity_host_manager/response/activity_manager_response.cpp",
            "activity_host_manager_request_reply",
            "server/bap/encrypted/activity_host_manager/activity_host_manager_route.cpp",
            false,
            ProtocolScope::activityHostAllocation,
            LifecycleScope::activityHost,
            false,
            AcceptancePolicy::bestEffortStateFallbackReply},
    Service{"bap.activity_message_request.v1",
            static_cast<std::uint16_t>(RequestService::activityMessage),
            kAbsentServiceId,
            ServiceRole::request,
            Security::encrypted,
            ResponseMode::oneWay,
            BodyShape::boundedTypedPayload,
            BodyShape::none,
            kActivityMessageRequestPrefixSize,
            kAbsentBodyExtent,
            activity_message::kMaximumPayloadSize,
            "activity_message_request_envelope_v1",
            "middleware/bap/activity_message/activity_message_request_parser.cpp",
            "none",
            "",
            "activity_message_request_one_way",
            "server/bap/encrypted/activity_message/activity_message_route.cpp",
            true,
            ProtocolScope::activityMessageIngress,
            LifecycleScope::none,
            false,
            AcceptancePolicy::strictOneWayEnvelope,
            kCompactActivityMessageRequestPrefixSize,
            kActivityMessageRequestCompleteEncryptedFrameOverhead,
            kActivityMessageRequestCompleteEncryptedFrameMaximum},
    Service{"bap.activity_message_notification.v1",
            static_cast<std::uint16_t>(NotificationService::activityMessage),
            kAbsentServiceId,
            ServiceRole::notification,
            Security::encrypted,
            ResponseMode::notification,
            BodyShape::boundedTypedPayload,
            BodyShape::none,
            kActivityMessageNotificationPrefixSize,
            kAbsentBodyExtent,
            activity_message::kMaximumPayloadSize,
            "activity_message_notification_envelope_v1",
            "middleware/bap/activity_message/activity_message_notification_encoder.cpp",
            "none",
            "",
            "activity_message_notification",
            "server/bap/encrypted/push/activity/activity_notification_frame.cpp",
            true,
            ProtocolScope::activityMessageNotification,
            LifecycleScope::none,
            false,
            AcceptancePolicy::outboundOnly,
            kAbsentBodyExtent,
            kActivityMessageNotificationCompleteEncryptedFrameOverhead,
            kActivityMessageNotificationCompleteEncryptedFrameMaximum},
    Service{"bap.activity_host_endpoint.v1",
            static_cast<std::uint16_t>(RequestService::activityHost),
            static_cast<std::uint16_t>(ResponseService::activityHost),
            ServiceRole::request,
            Security::encrypted,
            ResponseMode::correlated,
            BodyShape::fixed,
            BodyShape::fixed,
            activity_host::kRequestBodySize,
            activity_host::kResponseBodySize,
            0,
            "activity_host_endpoint_request_8",
            "middleware/bap/activity_host/activity_host_response.cpp",
            "activity_host_endpoint_response_16",
            "middleware/bap/activity_host/activity_host_response.cpp",
            "activity_host_endpoint_request_reply",
            "server/bap/encrypted/routing/bap_service_routing.cpp",
            false,
            ProtocolScope::activityHostLookup,
            LifecycleScope::activityHost,
            false,
            AcceptancePolicy::thinReplyOnBodyRefusal},
    Service{"bap.secure_channel_hello.v1",
            static_cast<std::uint16_t>(RequestService::serverHello),
            static_cast<std::uint16_t>(ResponseService::serverHello),
            ServiceRole::request,
            Security::plaintext,
            ResponseMode::correlated,
            BodyShape::fixed,
            BodyShape::fixed,
            kServerHelloRequestBodySize,
            kServerHelloResponseBodySize,
            0,
            "secure_channel_hello_request_v1_36",
            "server/bap/plaintext.cpp",
            "secure_channel_hello_response_v1_84",
            "middleware/secure_channel/envelope.cpp",
            "secure_channel_hello_request_reply",
            "server/bap/plaintext.cpp",
            false,
            ProtocolScope::secureChannelBootstrap,
            LifecycleScope::secureChannel,
            false,
            AcceptancePolicy::bodyIgnoredBootstrapReply},
    Service{"bap.channel_start_echo.v1",
            static_cast<std::uint16_t>(RequestService::start),
            static_cast<std::uint16_t>(ResponseService::start),
            ServiceRole::request,
            Security::plaintext,
            ResponseMode::correlated,
            BodyShape::opaqueEcho,
            BodyShape::opaqueEcho,
            kAbsentBodyExtent,
            kAbsentBodyExtent,
            0,
            "channel_start_opaque_body",
            "middleware/bap/bap_frame.cpp",
            "channel_start_opaque_echo",
            "middleware/bap/bap_frame.cpp",
            "channel_start_echo_request_reply",
            "server/bap/plaintext.cpp",
            false,
            ProtocolScope::channelStart,
            LifecycleScope::secureChannel,
            false,
            AcceptancePolicy::opaqueEcho},
    Service{"bap.keepalive.v1",
            static_cast<std::uint16_t>(RequestService::echo),
            static_cast<std::uint16_t>(ResponseService::echo),
            ServiceRole::request,
            Security::encrypted,
            ResponseMode::correlated,
            BodyShape::opaque,
            BodyShape::fixed,
            kAbsentBodyExtent,
            0,
            0,
            "keepalive_opaque_request",
            "server/bap/encrypted/body/bap_service_body.cpp",
            "keepalive_empty_response",
            "server/bap/encrypted/body/bap_service_body.cpp",
            "encrypted_keepalive_request_reply",
            "server/bap/encrypted/routing/bap_service_routing.cpp",
            false,
            ProtocolScope::keepalive,
            LifecycleScope::transport,
            false,
            AcceptancePolicy::bodyIgnoredEmptyReply},
}};

/** @return Stable SDK schema for this global protocol catalog. */
[[nodiscard]] constexpr std::string_view schema() noexcept {
    return "sunrise-bap-service-transport-v1";
}

/** @return Every executable global BAP service row. */
[[nodiscard]] constexpr std::span<const Service> services() noexcept {
    return kServices;
}

/** @return The request row with this outer BAP id, or null. */
[[nodiscard]] constexpr const Service* find_request(std::uint16_t serviceId) noexcept {
    for (const Service& service : kServices) {
        if (service.role == ServiceRole::request && service.serviceId == serviceId) {
            return &service;
        }
    }
    return nullptr;
}

/** @return The response-paired request row with this outer BAP id, or null. */
[[nodiscard]] constexpr const Service* find_response(std::uint16_t serviceId) noexcept {
    if (serviceId == kAbsentServiceId) {
        return nullptr;
    }
    for (const Service& service : kServices) {
        if (service.responseServiceId == serviceId) {
            return &service;
        }
    }
    return nullptr;
}

/** @return The notification row with this outer BAP id, or null. */
[[nodiscard]] constexpr const Service* find_notification(std::uint16_t serviceId) noexcept {
    for (const Service& service : kServices) {
        if (service.role == ServiceRole::notification && service.serviceId == serviceId) {
            return &service;
        }
    }
    return nullptr;
}

/** @return Stable SDK spelling for one service role. */
[[nodiscard]] constexpr std::string_view stable_name(ServiceRole value) noexcept {
    switch (value) {
    case ServiceRole::request:
        return "request";
    case ServiceRole::notification:
        return "notification";
    }
    return {};
}

/** @return Stable SDK spelling for one transport security state. */
[[nodiscard]] constexpr std::string_view stable_name(Security value) noexcept {
    switch (value) {
    case Security::plaintext:
        return "plaintext";
    case Security::encrypted:
        return "encrypted";
    }
    return {};
}

/** @return Stable SDK spelling for one response relationship. */
[[nodiscard]] constexpr std::string_view stable_name(ResponseMode value) noexcept {
    switch (value) {
    case ResponseMode::correlated:
        return "correlated_response";
    case ResponseMode::oneWay:
        return "one_way_request";
    case ResponseMode::notification:
        return "notification";
    }
    return {};
}

/** @return Stable SDK spelling for one live route acceptance policy. */
[[nodiscard]] constexpr std::string_view stable_name(AcceptancePolicy value) noexcept {
    switch (value) {
    case AcceptancePolicy::bestEffortStateFallbackReply:
        return "best_effort_state_fallback_reply";
    case AcceptancePolicy::strictOneWayEnvelope:
        return "strict_one_way_envelope";
    case AcceptancePolicy::outboundOnly:
        return "outbound_only";
    case AcceptancePolicy::thinReplyOnBodyRefusal:
        return "thin_reply_on_body_refusal";
    case AcceptancePolicy::bodyIgnoredBootstrapReply:
        return "body_ignored_bootstrap_reply";
    case AcceptancePolicy::opaqueEcho:
        return "opaque_echo";
    case AcceptancePolicy::bodyIgnoredEmptyReply:
        return "body_ignored_empty_reply";
    }
    return {};
}

/** @return Stable SDK spelling for one body shape. */
[[nodiscard]] constexpr std::string_view stable_name(BodyShape value) noexcept {
    switch (value) {
    case BodyShape::none:
        return "none";
    case BodyShape::fixed:
        return "fixed";
    case BodyShape::boundedTypedPayload:
        return "bounded_typed_payload";
    case BodyShape::opaque:
        return "opaque";
    case BodyShape::opaqueEcho:
        return "opaque_echo";
    }
    return {};
}

/** @return Stable SDK spelling for one protocol-owned service scope. */
[[nodiscard]] constexpr std::string_view stable_name(ProtocolScope value) noexcept {
    switch (value) {
    case ProtocolScope::activityHostAllocation:
        return "activity_host_allocation";
    case ProtocolScope::activityMessageIngress:
        return "activity_message_ingress";
    case ProtocolScope::activityMessageNotification:
        return "activity_message_notification";
    case ProtocolScope::activityHostLookup:
        return "activity_host_lookup";
    case ProtocolScope::secureChannelBootstrap:
        return "secure_channel_bootstrap";
    case ProtocolScope::channelStart:
        return "channel_start";
    case ProtocolScope::keepalive:
        return "keepalive";
    }
    return {};
}

/** @return Stable SDK spelling for one protocol lifecycle family. */
[[nodiscard]] constexpr std::string_view stable_name(LifecycleScope value) noexcept {
    switch (value) {
    case LifecycleScope::none:
        return "none";
    case LifecycleScope::activityHost:
        return "activity_host";
    case LifecycleScope::secureChannel:
        return "secure_channel";
    case LifecycleScope::transport:
        return "transport";
    }
    return {};
}

/** Rejects duplicate ids, invented body extents, and any script-sendable protocol row. */
[[nodiscard]] constexpr bool valid() noexcept {
    for (std::size_t index = 0; index < kServices.size(); ++index) {
        const Service& service = kServices[index];
        const bool bodyHasExtent = service.bodyShape == BodyShape::fixed
                                   || service.bodyShape == BodyShape::boundedTypedPayload;
        const bool responseHasExtent = service.responseBodyShape == BodyShape::fixed;
        const bool hasAlternateExtent = service.alternateBodyExtent != kAbsentBodyExtent;
        const bool hasCompleteFrameOverhead =
            service.completeEncryptedFrameOverhead != kAbsentBodyExtent;
        const bool hasCompleteFrameMaximum =
            service.completeEncryptedFrameMaximum != kAbsentBodyExtent;
        if (service.id.empty() || service.bodyCodec.empty() || service.route.empty()
            || service.bodyCodecPath.empty() || service.routePath.empty() || service.scriptSendable
            || stable_name(service.acceptancePolicy).empty()
            || (service.responseServiceId == kAbsentServiceId)
                   != (service.responseBodyShape == BodyShape::none)
            || bodyHasExtent != (service.bodyExtent != kAbsentBodyExtent)
            || (hasAlternateExtent
                && (service.bodyShape != BodyShape::boundedTypedPayload
                    || service.alternateBodyExtent >= service.bodyExtent))
            || responseHasExtent != (service.responseBodyExtent != kAbsentBodyExtent)
            || (service.bodyShape == BodyShape::boundedTypedPayload)
                   != (service.typedPayloadLimit != 0)
            || (service.activityMessageEnvelope
                && service.typedPayloadLimit != activity_message::kMaximumPayloadSize)
            || hasCompleteFrameOverhead != hasCompleteFrameMaximum
            || hasCompleteFrameOverhead != service.activityMessageEnvelope
            || (hasCompleteFrameMaximum
                && (service.security != Security::encrypted
                    || service.completeEncryptedFrameMaximum < service.typedPayloadLimit
                    || service.completeEncryptedFrameMaximum - service.typedPayloadLimit
                           != service.completeEncryptedFrameOverhead))) {
            return false;
        }
        for (std::size_t other = index + 1; other < kServices.size(); ++other) {
            if (service.serviceId == kServices[other].serviceId
                || service.id == kServices[other].id) {
                return false;
            }
        }
    }
    return true;
}

static_assert(kActivityMessageRequestPrefixSize == 21);
static_assert(kCompactActivityMessageRequestPrefixSize == 17);
static_assert(kActivityMessageNotificationPrefixSize == 17);
static_assert(kActivityMessageRequestCompleteEncryptedFrameOverhead == 49);
static_assert(kActivityMessageRequestCompleteEncryptedFrameMaximum == 514097);
static_assert(kActivityMessageNotificationCompleteEncryptedFrameOverhead == 45);
static_assert(kActivityMessageNotificationCompleteEncryptedFrameMaximum == 514093);
static_assert(valid());

} // namespace sunrise::middleware::bap::service_catalog
