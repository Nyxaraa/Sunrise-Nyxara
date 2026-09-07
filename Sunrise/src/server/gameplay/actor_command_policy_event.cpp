#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "../../state/activity_sdk/format.h"
#include "actor_command_policy_internal.h"

namespace sunrise::server::gameplay::actor_command_policy::internal {

namespace external = middleware::gameplay::external;
namespace wire = middleware::bap::activity_message::wire_schema;
namespace format = state::activity_sdk::format;

/** Reads the unique live target token from a typed DAMAGE event. */
bool damage_target(const external::DecodedRuntimeEvent& event,
                   external::EntityToken& output) noexcept {
    output = {};
    if (event.identity.secondarySchema == format::kAbsentIndex) {
        return false;
    }
    // Slot and incarnation are two values of one entity reference, so both carry its field row.
    bool slotFound = false;
    bool incarnationFound = false;
    std::uint32_t fieldRow = format::kAbsentIndex;
    for (std::size_t index = 0; index < event.secondary.count; ++index) {
        const wire::RuntimeDecodedValue& value = event.secondary.values[index];
        if (!value.present) {
            continue;
        }
        if (value.role == wire::ValueRole::entityReferenceSlot
            && value.unsignedValue <= external::kMaximumEntitySlot) {
            output.slot = static_cast<std::uint16_t>(value.unsignedValue);
            fieldRow = value.fieldRow;
            slotFound = true;
        } else if (value.role == wire::ValueRole::entityReferenceIncarnation
                   && value.fieldRow == fieldRow
                   && value.unsignedValue <= external::kMaximumEntityIncarnation) {
            output.incarnation = static_cast<std::uint8_t>(value.unsignedValue);
            incarnationFound = true;
        }
    }
    return slotFound && incarnationFound;
}

/** Decodes one retained arena record through its SDK event definition. */
bool decode_event(const external::ActorCommandCatalog& catalog,
                  const external::SimulationEventBatch& batch,
                  const external::SimulationEventRecord& record,
                  external::DecodedRuntimeEvent& output) noexcept {
    output = {};
    const auto event =
        std::find_if(catalog.events.begin(), catalog.events.end(), [&record](const auto& row) {
            return row.eventType == record.eventType;
        });
    if (event == catalog.events.end()) {
        return false;
    }
    const std::uint32_t eventIndex = static_cast<std::uint32_t>(event - catalog.events.begin());
    return external::decode_runtime_event_record(catalog, eventIndex, batch, record, output);
}

} // namespace sunrise::server::gameplay::actor_command_policy::internal
