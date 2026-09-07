#pragma once

#include "../../../state/activity_sdk/generated_world/runtime.h"
#include "mission_script_manifest_sdk.h"

namespace sunrise::server::activity::mission::sdk_bridge {

/** Returns manifest resolvers whose context remains the caller-owned world view. */
[[nodiscard]] lua_vm::ManifestDefinitionApi manifest_definition_api(
    const state::activity_sdk::generated_world::GeneratedWorldView& world) noexcept;

} // namespace sunrise::server::activity::mission::sdk_bridge
