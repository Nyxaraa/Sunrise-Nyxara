#include <algorithm>

#include "sensor_auth_update.h"
#include "roster_presence.h"

namespace sunrise::middleware::bap::activity_message::sensor_auth_update {
namespace {

namespace bits = encoding::bits;

/** The widest chunk the bit writer accepts in one call. */
constexpr std::uint8_t kChunkWidth = 32;

/** The type-17 lifetime slot. The spawn gate reads whichever type-17 registers first, so every
 *  one must carry its state, even on a mission-seed placeholder group. */
constexpr std::uint8_t kSlotTypeLifetime = 17;

/** @return The override when this is its one exact object slot. */
[[nodiscard]] const AuthOverride* matching_override(const Snapshot& snapshot,
                                                    std::uint32_t objectTag,
                                                    std::uint32_t key,
                                                    std::uint8_t slotType,
                                                    std::uint16_t slotIndex) noexcept {
    for (const AuthOverride& value : snapshot.authOverrides) {
        if (value.present && value.objectTag == objectTag && value.key == key
            && value.slotType == slotType && value.slotIndex == slotIndex) {
            return &value;
        }
    }
    return nullptr;
}

/** Writes one MSB-first packed body without copying its padded final bits. */
[[nodiscard]] bool write_packed(bits::Writer& writer, const AuthOverride& value) noexcept {
    bool encoded = true;
    std::size_t remaining = value.bitCount;
    for (std::size_t index = 0; encoded && remaining != 0; ++index) {
        const std::uint8_t width = static_cast<std::uint8_t>((std::min)(remaining, std::size_t{8}));
        std::uint8_t byte = std::to_integer<std::uint8_t>(value.body[index]);
        if (width < 8) {
            byte = static_cast<std::uint8_t>(byte >> (8 - width));
        }
        encoded = writer.write(byte, width);
        remaining -= width;
    }
    return encoded;
}

} // namespace

/** Writes zero bits in chunks the writer accepts. */
bool pad_bits(bits::Writer& writer, std::size_t count) noexcept {
    bool encoded = true;
    for (std::size_t written = 0; encoded && written < count; written += kChunkWidth) {
        const std::size_t remaining = count - written;
        const auto width =
            static_cast<std::uint8_t>(remaining > kChunkWidth ? kChunkWidth : remaining);
        encoded = writer.write(0, width);
    }
    return encoded;
}

/** Writes the bubble authority block. */
bool write_bubble_block(bits::Writer& writer, const Grant& grant) noexcept {
    bool encoded = true;
    for (std::size_t bubble = 0; encoded && bubble < kAuthoritySlotCount; ++bubble) {
        encoded = writer.write(bubble == grant.bubble ? 1U : 0U, kPresenceWidth);
    }
    // The block's own root presence bit, then the absent i32 header at struct +0.
    encoded = encoded && writer.write(1, kPresenceWidth) && writer.write(0, kPresenceWidth);
    for (std::size_t bubble = 0; encoded && bubble < kAuthoritySlotCount; ++bubble) {
        const bool granted = bubble == grant.bubble;
        // The host token stays absent. A wire copy that differs from the mirror parks a 5 s stamp.
        encoded =
            writer.write(0, kPresenceWidth) && writer.write(granted ? 1U : 0U, kPresenceWidth);
        if (encoded && granted) {
            encoded = writer.write(grant.token, kGrantTokenWidth);
        }
        // The commit bool has no presence bit, so all 65 of them are explicit zeros.
        encoded = encoded && writer.write(0, kPresenceWidth);
    }
    return encoded;
}

namespace {

/**
 * Writes one fixed key presence mask, low bit first.
 * A key whose bit is clear is dropped in silence, so the mask has to match the key count exactly.
 * @param writer Body writer.
 * @param keyCount Keys the matching list carries.
 * @param wordCount Words in the schema's fixed mask.
 * @return True when the whole mask fits.
 */
[[nodiscard]] bool
write_key_mask(bits::Writer& writer, std::size_t keyCount, std::size_t wordCount) noexcept {
    if (keyCount > wordCount * kChunkWidth) {
        return false;
    }
    bool encoded = true;
    for (std::size_t word = 0; encoded && word < wordCount; ++word) {
        const std::size_t low = word * kChunkWidth;
        const std::size_t set = keyCount > low ? keyCount - low : 0;
        const std::size_t bits = set > kChunkWidth ? kChunkWidth : set;
        encoded = writer.write((std::uint64_t{1} << bits) - 1, kChunkWidth);
    }
    return encoded;
}

/** @return The revision owned by one roster key, or the snapshot-wide compatibility value. */
[[nodiscard]] std::uint8_t
group_state_sequence(const Roster& roster, std::uint32_t key, std::uint8_t fallback) noexcept {
    for (std::size_t index = 0; index < roster.groupCount; ++index) {
        const Group& group = roster.groups[index];
        if (group.key == key) {
            return group.hasStateSequence ? group.stateSequence : fallback;
        }
    }
    return fallback;
}

/**
 * Writes one per-bubble sub-block: the bubble it belongs to, then its keys with their mask bits
 * and state bytes. ClientRoster_ApplyDelta applies only the sub-block whose key equals the current
 * bubble index, which is what makes these keys bubble-local.
 * @param writer Body writer.
 * @param block Bubble and keys to publish.
 * @param stateSequence Value each state byte carries, so a re-send can force a re-add.
 * @return True when the whole sub-block fits.
 */
[[nodiscard]] bool write_bubble_sub_block(bits::Writer& writer,
                                          const BubbleSubBlock& block,
                                          const Roster& roster,
                                          std::uint8_t stateSequence) noexcept {
    const std::size_t keyCount = block.keys.size();
    const auto count = static_cast<std::uint32_t>(keyCount);
    bool encoded = writer.write(1, kPresenceWidth)
                   && writer.write(kBubbleKeyBias + block.bubble, kKeyWidth)
                   && writer.write(1, kPresenceWidth) && writer.write(1, kPresenceWidth)
                   && writer.write(count, kBubbleCountWidth);
    for (std::size_t index = 0; encoded && index < keyCount; ++index) {
        encoded = writer.write(block.keys[index], kKeyWidth);
    }
    encoded = encoded && writer.write(1, kPresenceWidth);
    for (std::size_t word = 0; encoded && word < kBubbleMaskWords; ++word)
        encoded = writer.write(presence_word(roster, block.keys, word), kChunkWidth);
    encoded = encoded && writer.write(1, kPresenceWidth) && writer.write(count, kBubbleCountWidth);
    for (std::size_t index = 0; encoded && index < keyCount; ++index) {
        encoded = writer.write(
            kStateByteBias + group_state_sequence(roster, block.keys[index], stateSequence), 8);
    }
    return encoded;
}

/**
 * Writes the delta's field-1 half: the sub-block count, then one element each.
 * @param writer Body writer positioned after the field's presence bit.
 * @param subBlocks Sub-blocks to publish, in bubble order.
 * @param stateSequence Value each state byte carries.
 * @return True when every sub-block fits.
 */
[[nodiscard]] bool write_bubble_sub_blocks(bits::Writer& writer,
                                           const Roster& roster,
                                           std::uint8_t stateSequence) noexcept {
    const std::span<const BubbleSubBlock> subBlocks = roster.bubbleSubBlocks;
    bool encoded = writer.write(static_cast<std::uint32_t>(subBlocks.size()), kBubbleCountWidth);
    for (std::size_t index = 0; encoded && index < subBlocks.size(); ++index) {
        encoded = write_bubble_sub_block(writer, subBlocks[index], roster, stateSequence);
    }
    return encoded;
}

} // namespace

/** Writes the phase-1 roster delta, which registers the group keys. */
bool write_roster_delta(bits::Writer& writer,
                        const Roster& roster,
                        std::uint8_t stateSequence) noexcept {
    const std::size_t root = writer.bit_count();
    const std::size_t keyCount = roster.topLevelGroupCount;
    // Clearing the root presence bit means nothing below it is read.
    bool encoded = writer.write(1, kPresenceWidth) && writer.write(1, kPresenceWidth)
                   && writer.write(1, kPresenceWidth)
                   && writer.write(static_cast<std::uint32_t>(keyCount), kDeltaCountWidth)
                   && writer.bit_count() == root + kDeltaKeysBit;
    for (std::size_t group = 0; encoded && group < keyCount; ++group) {
        encoded = writer.write(roster.groups[group].key, kKeyWidth);
    }
    encoded = encoded && writer.write(1, kPresenceWidth)
              && writer.bit_count() == root + delta_mask_bit(keyCount);
    // A key whose mask bit is clear is dropped in silence, so the mask must match the key count.
    encoded = encoded && write_key_mask(writer, keyCount, kDeltaMaskWords)
              && writer.write(1, kPresenceWidth)
              && writer.bit_count() == root + delta_state_count_bit(keyCount)
              && writer.write(static_cast<std::uint32_t>(keyCount), kDeltaCountWidth);
    for (std::size_t group = 0; encoded && group < keyCount; ++group) {
        const Group& row = roster.groups[group];
        encoded = writer.write(
            kStateByteBias + (row.hasStateSequence ? row.stateSequence : stateSequence), 8);
    }
    // Field 1 is the per-bubble sub-block half. Absent, it is one zero bit and 32 KB of the
    // client's roster struct stays untouched.
    const std::span<const BubbleSubBlock> subBlocks = roster.bubbleSubBlocks;
    encoded = encoded && writer.write(subBlocks.empty() ? 0U : 1U, kPresenceWidth);
    if (encoded && !subBlocks.empty()) {
        encoded = write_bubble_sub_blocks(writer, roster, stateSequence);
    }
    return encoded && writer.bit_count() == root + delta_bits(keyCount, subBlocks);
}

/** Writes one per-object state block. */
bool write_object_block(bits::Writer& writer,
                        const Snapshot& snapshot,
                        std::uint32_t objectTag,
                        std::uint32_t key,
                        std::uint8_t slotType,
                        std::uint16_t slotIndex,
                        std::uint8_t flags,
                        bool missionSeedOnly,
                        bool carriesPlayerKey) noexcept {
    const bool emitAuth = (flags & kSlotAuthFlag) != 0;
    const bool emitSense = (flags & kSlotSenseFlag) != 0;
    const AuthOverride* const override =
        emitAuth ? matching_override(snapshot, objectTag, key, slotType, slotIndex) : nullptr;
    // A mission-seed group is a placeholder with no default body. The spawn gate reads two of
    // them: the player key (this type-13) and the lifetime state (every type-17, because the
    // gate reads whichever registers first). Zeroing those strands the spawn.
    const bool spawnBearing = carriesPlayerKey || slotType == kSlotTypeLifetime;
    const std::size_t body = override != nullptr
                                 ? override->bitCount
                                 : (emitAuth && (!missionSeedOnly || spawnBearing)
                                        ? auth_body_bits(snapshot, slotType, carriesPlayerKey)
                                        : 0);
    const std::size_t remainder = (emitAuth ? 2U : 0U) + (emitSense ? 1U : 0U) + body;
    bool encoded = writer.write(1, kPresenceWidth) && writer.write(key, kKeyWidth)
                   && writer.write(std::uint32_t{slotType} + kSlotTypeBias, kSlotTypeWidth)
                   && writer.write(std::uint32_t{slotIndex} + kSlotIndexBias, kSlotIndexWidth)
                   && writer.write(static_cast<std::uint32_t>(remainder), kKeyWidth);
    const std::size_t start = writer.bit_count();
    if (encoded && emitAuth) {
        // A reset bit of zero on a first block decodes into a throwaway buffer and never seeds.
        encoded =
            writer.write(1, kPresenceWidth) && writer.write(body > 0 ? 1U : 0U, kPresenceWidth);
        if (encoded && body > 0) {
            encoded = override != nullptr
                          ? write_packed(writer, *override)
                          : write_auth_body(writer, snapshot, slotType, carriesPlayerKey);
        }
    }
    // A sense-present bit of one costs 35 more bits, not one, so it is always sent absent.
    if (encoded && emitSense) {
        encoded = writer.write(0, kPresenceWidth);
    }
    return encoded && writer.bit_count() == start + remainder;
}

} // namespace sunrise::middleware::bap::activity_message::sensor_auth_update
