#pragma once

#include <cstdint>
#include <array>
#include <cmath>
#include <span>
#include "../../../middleware/bap/activity_message/sense_update.h"

namespace sunrise::server::activity::mission {

// Sense .11 is the consumed-request list at native squad +612. Deaths and
// failed/cancelled placements advance it; a rise is not an actor spawn event.
[[nodiscard]] inline std::uint8_t read_squad_consumed_counts(
    std::span<const middleware::bap::activity_message::sense_update::DecodedValue> values,
    std::span<std::int32_t> output) noexcept {
    std::uint32_t length = 0, known = 0;
    std::array<std::int32_t, 8> counts{};
    for (const auto& value : values) {
        if (!value.present) continue;
        if (value.schemaRow == 0x80807ECFU && value.fieldOrdinal == 0) {
            if (value.unsignedValue > counts.size()) return 0;
            length = static_cast<std::uint32_t>(value.unsignedValue);
        } else if (value.schemaRow == 0x80809491U && value.fieldOrdinal == 0
                   && value.occurrence < counts.size()) {
            if (value.signedValue < 0 || value.signedValue > 0x7fffffff) return 0;
            counts[value.occurrence] = static_cast<std::int32_t>(value.signedValue);
            known |= 1U << value.occurrence;
        }
    }
    if (!length || length > output.size() || known != (1U << length) - 1) return 0;
    for (std::size_t i = 0; i < output.size(); ++i) output[i] = i < length ? counts[i] : 0;
    return static_cast<std::uint8_t>(length);
}

// Native 4EB8E0 publishes one saturated distance cost per authored objective group.
// 4E84E0 publishes root 1 only after the cost pass, echoing Auth root 13.
struct SquadObjectiveCosts {
    std::array<float, 24> values{};
    std::uint32_t known{};
    std::uint32_t revision{};
};
[[nodiscard]] inline bool update_squad_objective_costs(SquadObjectiveCosts& retained,
    std::span<const middleware::bap::activity_message::sense_update::DecodedValue> values,
    std::uint32_t root) noexcept {
    bool changed = false;
    for (const auto& v : values) {
        if (!v.present) continue;
        if (v.schemaRow == root && v.fieldOrdinal == 1 && v.signedValue >= 0
            && v.signedValue <= 0x7fffffff) {
            const auto revision = static_cast<std::uint32_t>(v.signedValue);
            changed |= retained.revision != revision;
            retained.revision = revision;
        } else if (v.schemaRow == 0x80807ECD && v.fieldOrdinal == 0 && v.occurrence < 24
                   && std::isfinite(v.realValue) && v.realValue >= 0 && v.realValue <= 2040) {
            const auto bit = 1U << v.occurrence;
            changed |= !(retained.known & bit) || retained.values[v.occurrence] != v.realValue;
            retained.known |= bit;
            retained.values[v.occurrence] = v.realValue;
        }
    }
    return changed;
}

// Optional schema fields still produce DecodedValue rows when their presence bit is false.
// Only a present root alive count is evidence of a changed population; zero is a real value.
[[nodiscard]] inline bool read_squad_alive(
    std::span<const middleware::bap::activity_message::sense_update::DecodedValue> values,
    std::uint32_t rootSchema, std::int32_t& alive) noexcept {
    for (const auto& value : values) {
        if (value.schemaRow == rootSchema && value.fieldOrdinal == 3 && value.present) {
            if (value.signedValue < 0 || value.signedValue > 63) return false;
            alive = static_cast<std::int32_t>(value.signedValue);
            return true;
        }
    }
    return false;
}

} // namespace sunrise::server::activity::mission
