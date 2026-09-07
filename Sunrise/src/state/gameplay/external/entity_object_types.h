#pragma once
#include <span>
#include <vector>

#include "entity_identity.h"
#include "entity_position_profiles.h"
namespace sunrise::state::gameplay::entity_object_types {
using Fingerprint = entity_position_profiles::Fingerprint;
/** Bounds one installed package-class catalogue and its shared-cache scratch. */
inline constexpr std::size_t kMaximumRows = 16384;
/** The native object-type table ends at system, type 28. */
inline constexpr std::uint8_t kMaximumObjectType = 28;
/** Each row has a reciprocal package RSAT/class link and a native object-type byte. */
struct Row final {
    std::uint32_t rsatTag{}, definitionTag{};
    std::uint8_t objectType{};
    bool operator==(const Row&) const = default;
};
using Rows = std::vector<Row>;
[[nodiscard]] bool validate(std::span<const Row>) noexcept;
[[nodiscard]] bool publish(Rows, const Fingerprint&) noexcept;
[[nodiscard]] bool restore(std::span<const Row>, const Fingerprint&) noexcept;
[[nodiscard]] bool confirm(const Fingerprint&) noexcept;
[[nodiscard]] bool available() noexcept;
[[nodiscard]] bool snapshot(std::span<Row>, std::size_t&, Fingerprint&) noexcept;
[[nodiscard]] bool lookup(std::uint32_t rsatTag, Row&) noexcept;
/** Derives only missing class facts on a policy-owned copy of accepted identities. */
[[nodiscard]] bool enrich_snapshot(std::span<entity_identity::Identity>) noexcept;
void reset() noexcept;
} // namespace sunrise::state::gameplay::entity_object_types
