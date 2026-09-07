#pragma once

#include <cstdint>

#include "../../state/build_data/scriptables/definition.h"
#include "host_runtime.h"

namespace sunrise::server::activity::scriptable_override {

/** Why one package row can or cannot use the existing msg-5 roster path. */
enum class Eligibility : std::uint8_t {
    eligible,
    staleBinding,
    wrongScenario,
    incomplete,
    unstable,
    unsupportedSchema,
    noEffectiveRegion,
    noRosterGroup,
    inactiveBubble,
    ambiguousRosterGroup,
};

/** Exact canonical target and the result of resolving it. */
struct Resolution final {
    host::ScriptableTarget target{};
    Eligibility eligibility{Eligibility::noRosterGroup};
};

/**
 * Joins one package-derived slot to an existing canonical roster row.
 * It never creates a group and accepts bubble-local rows only while their bubble is reported.
 * @param binding Exact Activity Host generation selected in the panel.
 * @param snapshot Immutable dynamic package catalog for that destination.
 * @param objectRow Selected object row.
 * @param slotRow Selected slot row owned by the object.
 * @param effectiveRegion Exact region selected from the unique BAP ActivityClient.
 * @param output Cleared, then receives the canonical roster indices and status.
 * @return True only when the current destination already registers this exact full layout.
 */
[[nodiscard]] bool resolve(const state::activity::SessionBinding& binding,
                           const state::build_data::scriptables::Snapshot& snapshot,
                           std::uint32_t objectRow,
                           std::uint32_t slotRow,
                           std::int32_t effectiveRegion,
                           Resolution& output) noexcept;

/** @return Stable UI explanation for one eligibility result. */
[[nodiscard]] const char* eligibility_name(Eligibility value) noexcept;

} // namespace sunrise::server::activity::scriptable_override
