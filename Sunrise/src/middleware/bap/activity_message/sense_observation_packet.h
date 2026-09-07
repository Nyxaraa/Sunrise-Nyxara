#pragma once

#include <cstddef>

#include "sense_update.h"

namespace sunrise::middleware::bap::activity_message::sense_update {

/** Complete, or partial: the envelope closed but one delimited group had an unsupported member. */
[[nodiscard]] inline bool observation_status(DecodeStatus status) noexcept {
    return status == DecodeStatus::complete || status == DecodeStatus::partial;
}

/**
 * Tests whether every decoded object of a packet owns a bounded value range and a generation.
 * A complete packet may hold no undecoded object; a partial one may, and those are skipped.
 */
[[nodiscard]] inline bool observation_packet(const DecodedPacket& packet) noexcept {
    if (!observation_status(packet.status) || packet.objectsTruncated || packet.valuesTruncated
        || packet.objectCount > packet.objects.size() || packet.valueCount > packet.values.size()) {
        return false;
    }
    std::size_t end = 0;
    std::size_t decoded = 0;
    for (std::size_t index = 0; index < packet.objectCount; ++index) {
        const DecodedObject& object = packet.objects[index];
        if (object.firstValue != end || object.valueCount > packet.valueCount - end
            || object.status == ObjectStatus::malformed) {
            return false;
        }
        end += object.valueCount;
        if (object.status == ObjectStatus::decoded) {
            if (!object.hasGeneration) {
                return false;
            }
            ++decoded;
        } else if (packet.status == DecodeStatus::complete) {
            return false;
        }
    }
    return end == packet.valueCount && decoded == packet.objectsDecoded;
}

} // namespace sunrise::middleware::bap::activity_message::sense_update
