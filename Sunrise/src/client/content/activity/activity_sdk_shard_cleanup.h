#pragma once

#include <span>
#include <string>

#include "../../../state/activity_sdk/generated_world/catalog_manifest.h"

namespace sunrise::client::content::activity::sdk_generation {

/** Removes only stale files matching the generator's exact owned filename shapes. */
[[nodiscard]] bool clean_stale_shards(
    const std::wstring& directory,
    std::span<const state::activity_sdk::generated_world::manifest::Record> active) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation
