#include "player_block.h"

namespace sunrise::middleware::gameplay::group {

namespace {

namespace bits = encoding::bits;

/** Every field of both blocks sits behind its own presence bit. */
constexpr std::uint8_t kFlagWidth = 1;
/** Value a clear presence bit carries. */
constexpr std::uint64_t kFieldAbsent = 0;
/** See kFieldAbsent. */
constexpr std::uint64_t kFieldPresent = 1;

/** The name is 16-bit characters and ends at a zero character or at 64 characters. */
constexpr std::uint8_t kNameCharacterWidth = 16;
/** See kNameCharacterWidth. */
constexpr std::size_t kNameCapacity = 64;
/** The block's `+128` field, a fixed-width blob. */
constexpr std::size_t kIdentityBlobWidth = 288;
/** The block's `+176` field, stored as a word. */
constexpr std::size_t kSmallValueWidth = 8;
/** The block's `+178` and `+179` fields, both biased indices. */
constexpr std::size_t kIndexWidth = 6;
/** The block's `+184` field, a pair of signed 16-bit values. */
constexpr std::size_t kSignedPairWidth = 32;
/** Each half of the soid pair. */
constexpr std::uint8_t kSoidWidth = 64;
/** Fields the block carries before the soid pair, the name included. */
constexpr std::size_t kFieldsBeforePair = 6;
/** Fields the block carries after the soid pair. The second is a tag reflection with no writer
 *  here, so only its clear presence bit is published. */
constexpr std::size_t kFieldsAfterPair = 2;
/** Fields a 136-byte player delta carries. */
constexpr std::size_t kDeltaFieldCount = 4;

/** The tail's three leading words. */
constexpr std::uint8_t kTailWordWidth = 32;
/** See kTailWordWidth. */
constexpr std::size_t kTailWordCount = 3;
/** The tail's flags field. Its 0x10 bit gates one more field, so clear flags end the tail. */
constexpr std::uint8_t kTailFlagsWidth = 5;
/** The tail's kind field. */
constexpr std::uint8_t kTailKindWidth = 2;
/** Value every cleared tail field takes. */
constexpr std::uint64_t kTailCleared = 0;

/**
 * Consumes the wide name.
 * The producer stops at a zero character and also after 64 characters with no terminator.
 * @param reader Reader positioned at the first character.
 * @return True when the whole name was available.
 */
[[nodiscard]] bool skip_name(bits::Reader& reader) noexcept {
    for (std::size_t index = 0; index < kNameCapacity; ++index) {
        std::uint64_t character = 0;
        if (!reader.read(kNameCharacterWidth, character)) {
            return false;
        }
        if (character == 0) {
            return true;
        }
    }
    return true;
}

/**
 * Consumes one presence bit and the fixed-width field behind it.
 * @param reader Reader positioned at the presence bit.
 * @param width Bits the field occupies when present.
 * @return True when the bit and any present field were available.
 */
[[nodiscard]] bool skip_optional(bits::Reader& reader, std::size_t width) noexcept {
    std::uint64_t present = 0;
    if (!reader.read(kFlagWidth, present)) {
        return false;
    }
    return present == 0 || reader.skip(width);
}

} // namespace

/** Reads a full-mode 232-byte player block up to and including its soid pair. */
bool read_player_block_soids(bits::Reader& reader, PlayerBlockSoids& output) noexcept {
    output = {};
    std::uint64_t hasName = 0;
    if (!reader.read(kFlagWidth, hasName)) {
        return false;
    }
    if (hasName != 0 && !skip_name(reader)) {
        return false;
    }
    if (!skip_optional(reader, kIdentityBlobWidth) || !skip_optional(reader, kSmallValueWidth)
        || !skip_optional(reader, kIndexWidth) || !skip_optional(reader, kIndexWidth)
        || !skip_optional(reader, kSignedPairWidth)) {
        return false;
    }
    std::uint64_t hasPair = 0;
    if (!reader.read(kFlagWidth, hasPair)) {
        return false;
    }
    if (hasPair == 0) {
        return true;
    }
    if (!reader.read(kSoidWidth, output.accountSoid)
        || !reader.read(kSoidWidth, output.characterSoid)) {
        return false;
    }
    output.present = true;
    return true;
}

/** Writes a full-mode 232-byte player block carrying the soid pair and nothing else. */
bool write_player_block_soids(bits::Writer& writer, const PlayerBlockSoids& soids) noexcept {
    for (std::size_t field = 0; field < kFieldsBeforePair; ++field) {
        if (!writer.write(kFieldAbsent, kFlagWidth)) {
            return false;
        }
    }
    if (!writer.write(soids.present ? kFieldPresent : kFieldAbsent, kFlagWidth)) {
        return false;
    }
    if (soids.present
        && (!writer.write(soids.accountSoid, kSoidWidth)
            || !writer.write(soids.characterSoid, kSoidWidth))) {
        return false;
    }
    for (std::size_t field = 0; field < kFieldsAfterPair; ++field) {
        if (!writer.write(kFieldAbsent, kFlagWidth)) {
            return false;
        }
    }
    return true;
}

/** Writes a 136-byte player delta with every field absent. */
bool write_player_block_delta_absent(bits::Writer& writer) noexcept {
    for (std::size_t field = 0; field < kDeltaFieldCount; ++field) {
        if (!writer.write(kFieldAbsent, kFlagWidth)) {
            return false;
        }
    }
    return true;
}

/** Writes a 20-byte player tail with every field cleared. */
bool write_player_tail_cleared(bits::Writer& writer) noexcept {
    for (std::size_t word = 0; word < kTailWordCount; ++word) {
        if (!writer.write(kTailCleared, kTailWordWidth)) {
            return false;
        }
    }
    return writer.write(kTailCleared, kTailFlagsWidth) && writer.write(kTailCleared, kTailKindWidth)
           && writer.write(kTailCleared, kFlagWidth);
}

} // namespace sunrise::middleware::gameplay::group
