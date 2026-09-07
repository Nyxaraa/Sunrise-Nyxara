#pragma once

#include "../../../state/activity_sdk/runtime.h"
#include "mission_script_catalog_sdk.h"

namespace sunrise::server::activity::mission::sdk_bridge {

/** Copies every field that owns one immutable runtime-pack handle generation. */
[[nodiscard]] bool catalog_generation_identity(const state::activity_sdk::BoundView& view,
                                               lua_vm::CatalogGenerationIdentity& output) noexcept;

/** Returns lossless read-only row resolvers for one exact runtime-pack generation. */
[[nodiscard]] lua_vm::CatalogDefinitionApi
catalog_definition_api(const state::activity_sdk::BoundView& view) noexcept;

} // namespace sunrise::server::activity::mission::sdk_bridge
