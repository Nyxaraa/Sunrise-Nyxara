#pragma once

#include <cstdint>

#include "../../middleware/bap/activity_message/activity_patch_epoch_parser.h"
#include "../../middleware/bap/activity_message/sense_roster.h"

namespace sunrise::server::bap {

/** Received roster image; this acknowledges roster reconciliation, not component Auth apply. */
struct ActivityRosterMirror final {
    middleware::bap::activity_message::sense_update::RosterMirror roster{};
    middleware::bap::activity_message::patch_epoch::PatchEpoch epoch{};
    std::uint64_t bindingGeneration{};
    std::uint64_t receivedRevision{};
    std::uint64_t clientMessageSequence{};
    std::int32_t instantiatedRegion{-1};
    std::int32_t currentRegion{-1};
};

} // namespace sunrise::server::bap
