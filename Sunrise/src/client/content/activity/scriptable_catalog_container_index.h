#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "../../../middleware/content/packages/reader/reader.h"
#include "../../../state/build_data/scenarios/definition.h"

namespace sunrise::client::content::activity::scriptables::internal {

using ContainerIndexCancelCheck = bool (*)() noexcept;

/** One installed container tag with the normalized stem of the package family holding it. */
struct ContainerIndexEntry final {
    std::uint32_t tag{};
    std::array<char, state::build_data::scenarios::kSpawnStemCapacity> stem{};
    std::uint8_t stemLength{};
    /** False when the package family has no canonical stem, so no scenario can select it. */
    bool stemValid{};
};

/**
 * Every installed container tag, swept once and reused by every scenario of one pass.
 * The sweep result does not depend on the scenario, so it must run once per pass.
 */
struct ContainerIndex final {
    std::vector<ContainerIndexEntry> entries{};
    /** False when the class sweep itself refused, which every consumer reports as unresolved. */
    bool complete{};
    /** False when at least one package family had no canonical stem. */
    bool stemsComplete{};
};

/**
 * Sweeps the install once for the container class and keeps each tag with its stem.
 * @param source Package directory and borrowed block keys.
 * @param output Receives the entries and the two completeness flags.
 * @param cancel Optional cancellation probe.
 * @return True when the sweep finished, cancelled or not.
 */
[[nodiscard]] bool
build_container_index(const middleware::content::packages::reader::Source& source,
                      ContainerIndex& output,
                      ContainerIndexCancelCheck cancel) noexcept;

} // namespace sunrise::client::content::activity::scriptables::internal
