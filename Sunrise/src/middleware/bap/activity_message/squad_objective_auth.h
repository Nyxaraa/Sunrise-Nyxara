#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../encoding/bit_writer.h"
#include "auth_fields.h"
#include "scriptable_auth_body.h"
#include "squad_auth_body.h"

// Type-1 squad Auth that assigns the squad to a native combat objective. Root .0 names the
// objective, .13 carries the revision that invalidates its cost pass, .16 links a task group.
// Counts, profile and spawn generation stay absent so the placement is untouched.

namespace sunrise::middleware::bap::activity_message::squad_objective {

namespace fields = auth_fields;

inline constexpr std::uint32_t kSchema = squad_auth::kSchema;
inline constexpr std::size_t kBits = 153;
inline constexpr std::size_t kBytes = 20;
/** Absent presence bits for fields .1 to .12. */
inline constexpr std::uint8_t kAbsentMiddleFieldCount = 12;
/** Root .15 is present and zero. Meaning unverified. */
inline constexpr std::uint8_t kField15Width = 6;
/** Root .16 task group: 5 bits with bias one, so -1 requests costs without a link. */
inline constexpr std::uint8_t kTaskGroupWidth = 5;
inline constexpr std::int32_t kTaskGroupBias = 1;
inline constexpr std::int32_t kNoTaskGroup = -1;
/** One objective sensor carries this many task groups. */
inline constexpr std::int32_t kTaskGroupCount = 24;
/** Root .18 active, 2 bits, written as the active wire value. */
inline constexpr std::uint8_t kActiveWidth = 2;
inline constexpr std::uint32_t kActiveValue = 2;
/** Root .19 mode, 3 bits with bias one. Reserve keeps the counts for a delivery. */
inline constexpr std::uint8_t kModeWidth = 3;
inline constexpr std::uint32_t kModeBias = 1;

struct Request final {
    std::uint32_t registryKey{};
    std::uint32_t revision{};
    std::uint16_t objectiveIndex{};
    std::int32_t taskGroup{kNoTaskGroup};
    bool reserved{};
};

/**
 * Encodes the objective assignment.
 * @param output Exactly kBytes.
 * @return False on an out-of-range revision, index or task group.
 */
[[nodiscard]] inline bool encode(const Request& request, std::span<std::byte> output) noexcept {
    if (output.size() != kBytes || request.registryKey == 0 || request.revision == 0
        || request.revision > fields::kMaximumCounter
        || request.objectiveIndex > fields::kMaximumClientRefIndex
        || request.taskGroup < kNoTaskGroup || request.taskGroup >= kTaskGroupCount) {
        return false;
    }
    const auto mode = static_cast<std::uint32_t>(request.reserved ? squad_auth::Mode::reserve
                                                                  : squad_auth::Mode::mode2);
    encoding::bits::Writer writer(output);
    std::size_t written = 0;
    const std::array<fields::Field, 13> tail{{
        {0, kAbsentMiddleFieldCount},
        {1, fields::kPresenceWidth}, // .13 present
        {request.revision, fields::kCounterWidth},
        {0, fields::kPresenceWidth}, // .14 absent
        {1, fields::kPresenceWidth}, // .15 present
        {0, kField15Width},
        {1, fields::kPresenceWidth}, // .16 present
        {static_cast<std::uint32_t>(request.taskGroup + kTaskGroupBias), kTaskGroupWidth},
        {0, fields::kPresenceWidth}, // .17 absent
        {kActiveValue, kActiveWidth},
        {mode + kModeBias, kModeWidth},
        {1, fields::kPresenceWidth},       // .20 present
        {fields::kClientRefAbsentKey, 32}, // no name
    }};
    return writer.write(1, fields::kPresenceWidth)
           && fields::write_client_ref(
               writer, request.registryKey, scriptable_auth::kType3SlotType, request.objectiveIndex)
           && fields::write_fields(writer, tail)
           && fields::finish_exact(writer, kBits, kBytes, written);
}

} // namespace sunrise::middleware::bap::activity_message::squad_objective
