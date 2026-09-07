#pragma once

#include <cstdint>

#include "../../../middleware/bap/activity_message/cinematic_incident.h"
#include "../../../state/build_data/scriptables/definition.h"

namespace sunrise::server::activity::mission::cinematic {

enum class ResolveStatus : std::uint8_t {
    ready,
    absent,
    ambiguous,
    invalidCatalog,
};

/** Exact authored Type-6 slot named by one native cinematic incident. */
struct Source final {
    std::uint32_t registryKey{};
    std::uint32_t objectTag{};
    std::uint32_t objectRow{};
    std::uint32_t slotRow{};
    std::uint16_t slotIndex{};
    std::uint8_t slotType{};
};

/** Resolves one cinematic incident's embedded ClientRef against the authored catalog. */
[[nodiscard]] ResolveStatus
resolve(const state::build_data::scriptables::Snapshot& snapshot,
        const middleware::bap::activity_message::cinematic_incident::Payload& target,
        Source& output) noexcept;

} // namespace sunrise::server::activity::mission::cinematic
