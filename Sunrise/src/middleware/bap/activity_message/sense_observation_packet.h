#pragma once
#include "sense_update.h"

namespace sunrise::middleware::bap::activity_message::sense_update {
// Partial means the complete envelope closed, but a length-delimited group had
// an unsupported member. Other fully decoded objects still carry valid receipts.
[[nodiscard]] inline bool observation_status(DecodeStatus status) noexcept {
    return status == DecodeStatus::complete || status == DecodeStatus::partial;
}
[[nodiscard]] inline bool observation_packet(const DecodedPacket& packet) noexcept {
    if (!observation_status(packet.status) || packet.objectsTruncated || packet.valuesTruncated
        || packet.objectCount > packet.objects.size() || packet.valueCount > packet.values.size()) return false;
    std::size_t end{}, decoded{};
    for (std::size_t i=0; i<packet.objectCount; ++i) {
        const auto& object=packet.objects[i];
        if (object.firstValue != end || object.valueCount > packet.valueCount-end
            || object.status == ObjectStatus::malformed) return false;
        end += object.valueCount;
        if (object.status == ObjectStatus::decoded) {
            if (!object.hasGeneration) return false;
            ++decoded;
        } else if (packet.status == DecodeStatus::complete) return false;
    }
    return end == packet.valueCount && decoded == packet.objectsDecoded;
}
}
