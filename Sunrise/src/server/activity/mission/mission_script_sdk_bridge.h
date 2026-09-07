#pragma once

#include "../../../state/activity_sdk/generated_world/runtime.h"
#include "../../../state/activity_sdk/runtime.h"
#include "mission_script_vm.h"

namespace sunrise::server::activity::mission::sdk_bridge {

/** Copies one arbitrary authenticated activity row into the shared Lua binding shape. */
[[nodiscard]] bool
activity_binding_definition(const state::activity_sdk::Catalog& catalog,
                            const state::activity_sdk::format::Activity& activity,
                            lua_vm::ActivityBindingDefinition& output) noexcept;
/** Returns one arbitrary activity row's exact canonical binding-tag range. */
[[nodiscard]] std::span<const state::activity_sdk::format::ActivityBindingTag>
activity_binding_tags(const state::activity_sdk::Catalog& catalog,
                      const state::activity_sdk::format::Activity& activity,
                      lua_vm::ActivityBindingTagKind kind) noexcept;

/** Builds the exact Lua declaration identity from one pinned activity view. */
[[nodiscard]] bool program_identity(const state::activity_sdk::BoundView& view,
                                    bool publicTarget,
                                    lua_vm::ProgramIdentity& output) noexcept;
/** Returns value resolvers whose context remains the caller-owned bound view. */
[[nodiscard]] lua_vm::DefinitionApi
definition_api(const state::activity_sdk::BoundView& view) noexcept;
/** Returns value resolvers for one exact activity and generated-world generation. */
[[nodiscard]] lua_vm::DefinitionApi
definition_api(const state::activity_sdk::BoundView& view,
               const state::activity_sdk::generated_world::GeneratedWorldView& world) noexcept;
/** Copies every field that owns generated-world handle lifetime. */
[[nodiscard]] bool
world_generation_identity(const state::activity_sdk::generated_world::GeneratedWorldView& world,
                          lua_vm::WorldGenerationIdentity& output) noexcept;
/** Hashes the complete world generation under the durable-program domain. */
[[nodiscard]] bool world_program_generation_sha256(
    const state::activity_sdk::generated_world::GeneratedWorldView& world,
    std::array<std::byte, 32>& output) noexcept;

} // namespace sunrise::server::activity::mission::sdk_bridge
