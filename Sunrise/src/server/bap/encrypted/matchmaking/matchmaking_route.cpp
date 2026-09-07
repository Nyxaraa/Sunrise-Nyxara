#include "matchmaking_route.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <limits>
#include <string_view>

#include "../../../../core/logging/log.h"
#include "../../../../middleware/bap/matchmaking/request/matchmaking_request_parser.h"
#include "../../../../middleware/bap/matchmaking/response/matchmaking_response_encoder.h"
#include "../../../../state/activity/defaults/activity_defaults_snapshot.h"
#include "../../../../state/activity_sdk/runtime.h"

namespace sunrise::server::bap::encrypted::matchmaking {
namespace {

namespace service = middleware::bap::matchmaking;

/** Middleware parsing and State storage must accept the same runtime descriptor size. */
static_assert(service::kJoinDescriptorSize == state::matchmaking::kDescriptorSize);

/**
 * Makes a kind-4 target the selected character's current activity when its row authors that.
 * The client latches its fly-in variant at its launch commit, which follows this request.
 * @param request Fully validated request fields.
 * @param currentActivity Cleared, then prepared when the character's value changes.
 */
void prepare_current_activity(const service::Request& request,
                              state::PendingCurrentActivity& currentActivity) noexcept {
    currentActivity = {};
    if (request.kind != service::RequestKind::configuration) {
        return;
    }
    if (!request.activity.hasDefinitionHash) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         "ev=bap svc=42 stage=configuration result=no_activity");
        return;
    }
    const state::activity_sdk::Snapshot catalog = state::activity_sdk::snapshot();
    const state::activity_sdk::format::Activity* row = nullptr;
    if (catalog != nullptr) {
        for (const state::activity_sdk::format::Activity& activity : catalog->activities()) {
            if (activity.definitionHash == request.activity.definitionHash) {
                row = &activity;
                break;
            }
        }
    }
    std::array<char, core::log::kLineCapacity> line{};
    if (row == nullptr) {
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "ev=bap svc=42 stage=configuration result=unknown_activity activity_hash=0x%08X",
            request.activity.definitionHash);
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        return;
    }
    const std::string_view name = catalog->string(row->internalName);
    const bool policy = state::activity::defaults::current_activity_from_launch(name);
    const bool changed = policy && row->activityIndex <= (std::numeric_limits<std::uint16_t>::max)()
                         && state::prepare_current_activity(
                             static_cast<std::uint16_t>(row->activityIndex), currentActivity);
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=bap svc=42 stage=configuration result=ok "
                                    "activity_hash=0x%08X type_hash=0x%08X activity=%u "
                                    "name=%.*s current_activity_policy=%u changed=%u",
                                    request.activity.definitionHash,
                                    request.activity.typeHash,
                                    row->activityIndex,
                                    static_cast<int>(name.size()),
                                    name.data(),
                                    policy ? 1U : 0U,
                                    changed ? 1U : 0U);
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(count)});
    }
}

/**
 * Finds the State-backed fields for the request kinds that are not static.
 * @param context Active logical matchmaking context.
 * @param request Fully validated request fields with a borrowed descriptor.
 * @param response Receives the response shape and any id State picked.
 * @param mutation Receives a prepared update without committing persistent State.
 * @return True when the request is static or all required State fields are available.
 */
[[nodiscard]] bool prepare_fields(state::matchmaking::ContextHandle context,
                                  const service::Request& request,
                                  service::Response& response,
                                  state::matchmaking::PendingMutation& mutation) noexcept {
    response.kind = request.kind;
    switch (request.kind) {
    case service::RequestKind::advertisementUpdate:
        return state::matchmaking::prepare_variant_update(context,
                                                          request.advertisement.existingId,
                                                          request.advertisement.variantKey,
                                                          request.advertisement.hasDescriptor,
                                                          request.advertisement.descriptor,
                                                          response.advertisementId,
                                                          mutation);
    case service::RequestKind::rejoinAdvertisementUpdate:
        return state::matchmaking::prepare_initial_latest(
            context, response.advertisementId, mutation);
    case service::RequestKind::none:
    case service::RequestKind::sessionSearch:
    case service::RequestKind::advertisementDelete:
    case service::RequestKind::configuration:
    case service::RequestKind::rejoinAdvertisementDelete:
    case service::RequestKind::locateSession:
    case service::RequestKind::liveStats:
        return true;
    }
    return false;
}

} // namespace

/** Prepares and encodes one kind-specific svc-43 response transaction. */
bool encode_response(state::matchmaking::ContextHandle context,
                     std::span<const std::byte> requestBody,
                     std::span<std::byte> output,
                     std::size_t& written,
                     state::matchmaking::PendingMutation& mutation,
                     bool& hasMutation,
                     state::PendingCurrentActivity& currentActivity) noexcept {
    written = 0;
    hasMutation = false;
    SecureZeroMemory(&mutation, sizeof mutation);
    const service::Request request = service::request::parse(requestBody);
    service::Response response{};
    if (!prepare_fields(context, request, response, mutation)) {
        // A correlated empty fallback clears the pending client task without publishing State.
        SecureZeroMemory(&mutation, sizeof mutation);
        response = {};
    }
    prepare_current_activity(request, currentActivity);

    state::matchmaking::LatestSnapshot latest{};
    if (response.kind == service::RequestKind::locateSession
        && state::matchmaking::latest_snapshot(context, latest)) {
        response.advertisementId = latest.advertisementId;
        if (latest.hasDescriptor) {
            response.descriptor = std::span(latest.descriptor);
        }
    }
    const bool encoded = service::response::encode(response, output, written);
    state::matchmaking::erase_snapshot(latest);
    if (!encoded) {
        written = 0;
        SecureZeroMemory(&mutation, sizeof mutation);
        return false;
    }
    hasMutation = mutation.kind != state::matchmaking::MutationKind::none;
    return true;
}

} // namespace sunrise::server::bap::encrypted::matchmaking
