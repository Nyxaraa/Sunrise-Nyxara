#include "mission_script_cinematic.h"

#include <cstddef>

namespace sunrise::server::activity::mission::cinematic {
namespace {

namespace catalog = state::build_data::scriptables;

// A cinematic incident names its source with slot type 6; any other type is not a cinematic.
constexpr std::uint8_t kCinematicSlotType = 6;

[[nodiscard]] bool same_source(const Source& left, const Source& right) noexcept {
    return left.registryKey == right.registryKey && left.objectTag == right.objectTag
           && left.objectRow == right.objectRow && left.slotRow == right.slotRow
           && left.slotIndex == right.slotIndex && left.slotType == right.slotType;
}

} // namespace

/** Resolves the cinematic one incident payload names. @return `absent` when it names none. */
ResolveStatus resolve(const catalog::Snapshot& snapshot,
                      const middleware::bap::activity_message::cinematic_incident::Payload& target,
                      Source& output) noexcept {
    output = {};
    if (target.slotType != kCinematicSlotType || target.slotIndex < 0) {
        return ResolveStatus::absent;
    }

    bool found = false;
    for (std::size_t slotRow = 0; slotRow < snapshot.slots.size(); ++slotRow) {
        const catalog::Slot& slot = snapshot.slots[slotRow];
        if (slot.objectRow >= snapshot.objects.size()) {
            return ResolveStatus::invalidCatalog;
        }
        const catalog::Object& object = snapshot.objects[slot.objectRow];
        // The ClientRef names exactly one authored slot. Region is not a filter: an
        // extra-ordinal state's key never equals a region the client reports.
        if (object.registryKey != target.registryKey || slot.slotType != kCinematicSlotType
            || slot.slotIndex != static_cast<std::uint16_t>(target.slotIndex)) {
            continue;
        }
        const Source candidate{object.registryKey,
                               object.objectTag,
                               slot.objectRow,
                               static_cast<std::uint32_t>(slotRow),
                               slot.slotIndex,
                               static_cast<std::uint8_t>(slot.slotType)};
        if (found && !same_source(output, candidate)) {
            output = {};
            return ResolveStatus::ambiguous;
        }
        output = candidate;
        found = true;
    }
    return found ? ResolveStatus::ready : ResolveStatus::absent;
}

} // namespace sunrise::server::activity::mission::cinematic
