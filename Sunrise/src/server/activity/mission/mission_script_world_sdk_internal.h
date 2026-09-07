#pragma once

#include "mission_script_world_sdk.h"

namespace sunrise::server::activity::mission::sdk_bridge::world_sdk_internal {

/** Counts one raw generated-world family after exact generation revalidation. */
[[nodiscard]] std::size_t world_count(const void* context,
                                      const lua_vm::WorldGenerationIdentity& generation,
                                      lua_vm::WorldCollectionKind kind) noexcept;

/** Resolves one raw generated-world field after exact generation revalidation. */
[[nodiscard]] bool resolve_world_field(const void* context,
                                       const lua_vm::WorldGenerationIdentity& generation,
                                       lua_vm::WorldCollectionKind kind,
                                       std::uint32_t localRow,
                                       std::string_view key,
                                       lua_vm::WorldFieldDefinition& output) noexcept;

} // namespace sunrise::server::activity::mission::sdk_bridge::world_sdk_internal
