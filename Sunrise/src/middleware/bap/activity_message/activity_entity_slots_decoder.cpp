#include "entity_slots.h"

namespace sunrise::middleware::bap::activity_message::entity_slots {
/**
 * Reads each numeric word before expanding its low-order slot bytes.
 * @param reader Wire reader at any bit offset; unchanged on failure.
 * @param mask Receives canonical slot bytes; unchanged on failure.
 * @return True when all 256 words are present.
 */
bool read_mask(encoding::bits::Reader& reader, EntitySlotMask& mask) noexcept {
    auto candidateReader = reader;
    EntitySlotMask candidate{};
    for (std::size_t word = 0; word < kWordCount; ++word) {
        std::uint64_t value{};
        if (!candidateReader.read(kBitsPerMaskWord, value)) {
            return false;
        }
        for (std::size_t byte = 0; byte < sizeof(std::uint32_t); ++byte) {
            candidate[word * sizeof(std::uint32_t) + byte] =
                static_cast<std::byte>(value >> (byte * kBitsPerMaskByte));
        }
    }
    mask = candidate;
    reader = candidateReader;
    return true;
}
/** Decodes the fixed big-endian word prefix into canonical slot bytes. */
bool decode_entity_slots(std::span<const std::byte> input, EntitySlotMask& mask) noexcept {
    mask = {};
    if (input.size() < kEncodedSize) {
        return false;
    }
    encoding::bits::Reader reader(input.first(kEncodedSize));
    return read_mask(reader, mask);
}
} // namespace sunrise::middleware::bap::activity_message::entity_slots
