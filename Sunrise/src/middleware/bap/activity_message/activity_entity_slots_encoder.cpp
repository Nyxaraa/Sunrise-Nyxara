#include "entity_slots.h"

namespace sunrise::middleware::bap::activity_message::entity_slots {
/**
 * Emits each canonical mask word as one unsigned 32-bit scalar.
 * @param writer Wire writer at any bit offset.
 * @param mask Complete canonical slot bytes.
 * @return False when the fixed array does not fit.
 */
bool write_mask(encoding::bits::Writer& writer,
                std::span<const std::byte, kEncodedSize> mask) noexcept {
    for (std::size_t word = 0; word < kWordCount; ++word) {
        std::uint32_t value{};
        for (std::size_t byte = 0; byte < sizeof(std::uint32_t); ++byte) {
            value |= std::to_integer<std::uint32_t>(mask[word * sizeof(std::uint32_t) + byte])
                     << (byte * kBitsPerMaskByte);
        }
        if (!writer.write(value, kBitsPerMaskWord)) {
            return false;
        }
    }
    return true;
}
/** Encodes canonical slot bytes as 256 big-endian words. */
bool encode_entity_slots(std::span<const std::byte, kEncodedSize> mask,
                         std::span<std::byte> output,
                         std::size_t& written) noexcept {
    written = {};
    if (output.size() < kEncodedSize) {
        return false;
    }
    encoding::bits::Writer writer(output.first(kEncodedSize));
    return write_mask(writer, mask) && writer.finish(written);
}
} // namespace sunrise::middleware::bap::activity_message::entity_slots
