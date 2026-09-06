#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

#include "scriptable_auth_internal.h"

// The scriptable-auth bodies whose encoded width is fixed by their schema. Each family has one
// encoder and one checker: authored objects, device channels, triggers, timers, sequences,
// cinematics, tasks, dialogue, HUD directives, the encounter observer and the public-event sensor.

namespace sunrise::middleware::bap::activity_message::scriptable_auth {
namespace {

namespace bits = encoding::bits;

/** Signed 16-bit schema fields store zero at the middle of the unsigned wire range. */
constexpr std::uint32_t kSigned16Bias = 0x8000;
constexpr std::uint8_t kSequenceWidth = 16;
constexpr std::uint8_t kWideIntegerWidth = 64;
constexpr std::uint64_t kUnusedAuxiliary = 0;
/** Client no-timer sentinel for the 0x808099C4 epoch field; 0 means a live 0-second timer. */
constexpr std::uint64_t kNoTimerEpoch = 0xFFFFFFFFFFFFFFFFULL;
constexpr std::uint8_t kSequenceInactiveRevision = 0xFFU;
constexpr std::size_t kSequenceReferenceCount = 128U;
constexpr std::uint8_t kType3ValueWidth = 7;
constexpr std::uint8_t kType3GenerationWidth = 31;
constexpr std::uint64_t kDialogueActiveWorld = 1;
constexpr std::int8_t kDialogueInactiveMode = -1;
constexpr std::int8_t kDialogueFireMode = 2;
constexpr std::uint8_t kDirectiveStateWidth = 2;
constexpr std::uint8_t kDirectiveAuxStateWidth = 3;
constexpr std::uint8_t kDirectiveActiveIndexWidth = 3;
/** Type-31 reserves the largest generation as its unset sentinel. */
constexpr std::uint64_t kReservedGeneration = std::numeric_limits<std::uint64_t>::max();

/**
 * Writes the 353-bit 0x808099C4 child with no timer: epoch -1, all other fields 0.
 * Epoch 0 arms the client's goal-timer pin and the directive banner never hides.
 */
[[nodiscard]] bool write_neutral_timed_state(bits::Writer& writer) noexcept {
    if (!writer.write(0, kBoolWidth)) {
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        if (!writer.write(0, kWideIntegerWidth)) {
            return false;
        }
    }
    return writer.write(kNoTimerEpoch, kWideIntegerWidth) && writer.write(0, kReal32Width);
}

/** Writes one neutral 239-bit authored HUD marker subrecord. */
[[nodiscard]] bool write_directive_marker(bits::Writer& writer, const Type2LaneClientRef& target) noexcept {
    // 100A6E0 copies .0 into the native target; .2's first hash must be the
    // absent FNV sentinel or the client attempts a second authored-name lookup.
    return writer.write(target.registryKey, 32)
           && writer.write(static_cast<std::uint8_t>(target.slotType), kClientRefTypeWidth)
           && writer.write(static_cast<std::uint32_t>(target.slotIndex + kClientRefIndexBias), kClientRefIndexWidth)
           && write_absent_client_ref(writer) && writer.write(kClientRefAbsentKey, 32)
           && writer.write(0, 32) && writer.write(0, 32) && writer.write(0, 32)
           && writer.write(0, kBoolWidth);
}

/** Writes one of the three complete type-68 state lanes. */
[[nodiscard]] bool write_directive_entry(bits::Writer& writer,
                                         std::uint32_t nameHash,
                                         std::int32_t elementIndex,
                                         std::int8_t state,
                                         const Type2LaneClientRef& target = {}) noexcept {
    if (!writer.write(nameHash, 32)
        || !writer.write(std::bit_cast<std::uint32_t>(elementIndex) + kSigned32Bias, 32)
        || !writer.write(static_cast<std::uint32_t>(state) + 1U, kDirectiveStateWidth)
        || !write_neutral_timed_state(writer)) {
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        if (!writer.write(kSigned32Bias, 32)) {
            return false;
        }
    }
    if (!writer.write(1, kDirectiveStateWidth) || !write_absent_client_ref(writer)
        || !writer.write(target.slotIndex >= 0 ? 3U : 1U, kDirectiveAuxStateWidth)) {
        return false;
    }
    for (std::size_t index = 0; index < 4; ++index) {
        if (!write_directive_marker(writer, index == 0 ? target : Type2LaneClientRef{})) {
            return false;
        }
    }
    return true;
}

/** @return The reflected channel ordinal, or the channel count for an invalid enum value. */
[[nodiscard]] constexpr std::size_t channel_index(Type23Channel channel) noexcept {
    const auto index = static_cast<std::size_t>(channel);
    return index < kType23ChannelCount ? index : kType23ChannelCount;
}

/** @return True when one type-23 preset names a channel and advances its sequence. */
[[nodiscard]] bool valid_type23(const Type23Preset& preset,
                                const Type23SequenceGuard& guard) noexcept {
    const std::size_t index = channel_index(preset.channel);
    if (index == kType23ChannelCount || guard.last[index] < 0) {
        return false;
    }
    return preset.sequence > 0 && preset.sequence > guard.last[index];
}

/** Writes all three reflected type-23 channels. */
[[nodiscard]] bool write_type23(bits::Writer& writer, const Type23Body& body) noexcept {
    bool encoded = true;
    for (std::size_t index = 0; encoded && index < kType23ChannelCount; ++index) {
        const Type23ChannelState& channel = body.channels[index];
        const std::int32_t wireSequence =
            static_cast<std::int32_t>(channel.sequence) + kSigned16Bias;
        encoded = writer.write(std::bit_cast<std::uint32_t>(channel.desiredValue), kReal32Width)
                  && writer.write(wireSequence, kSequenceWidth)
                  && writer.write(channel.snap ? 1U : 0U, kBoolWidth);
    }
    return encoded && writer.bit_count() == kType23BitCount;
}

/** Reads all three reflected type-23 channels; float values pass through unchanged. */
[[nodiscard]] bool read_type23(bits::Reader& reader, Type23Body& body) noexcept {
    Type23Body parsed{};
    for (Type23ChannelState& channel : parsed.channels) {
        std::uint64_t desired = 0;
        std::uint64_t sequence = 0;
        std::uint64_t snap = 0;
        if (!reader.read(kReal32Width, desired) || !reader.read(kSequenceWidth, sequence)
            || !reader.read(kBoolWidth, snap)) {
            return false;
        }
        channel.desiredValue = std::bit_cast<float>(static_cast<std::uint32_t>(desired));
        channel.sequence =
            static_cast<std::int16_t>(static_cast<std::int32_t>(sequence) - kSigned16Bias);
        channel.snap = snap != 0;
    }
    body = parsed;
    return true;
}

/** @return True when one type-31 generation passes the client's monotonic gate. */
[[nodiscard]] bool valid_type31(const Type31Preset& preset,
                                const Type31GenerationGuard& guard) noexcept {
    return preset.generation != kReservedGeneration
           && (!guard.hasLast || preset.generation > guard.last);
}

/** Writes the complete fixed-width 0x808099C4 child layout. */
[[nodiscard]] bool write_shared_timed_state(bits::Writer& writer,
                                            const SharedTimedState& state) noexcept {
    return writer.write(state.running ? 1U : 0U, kBoolWidth)
           && writer.write(state.minimum, kWideIntegerWidth)
           && writer.write(state.maximum, kWideIntegerWidth)
           && writer.write(state.currentAtEpoch, kWideIntegerWidth)
           && writer.write(state.remainingAtEpoch, kWideIntegerWidth)
           && writer.write(state.epoch, kWideIntegerWidth)
           && writer.write(std::bit_cast<std::uint32_t>(state.rate), kReal32Width);
}

/** Reads the complete fixed-width 0x808099C4 child layout. */
[[nodiscard]] bool read_shared_timed_state(bits::Reader& reader, SharedTimedState& state) noexcept {
    SharedTimedState parsed{};
    std::uint64_t flag = 0;
    if (!reader.read(kBoolWidth, flag)) {
        return false;
    }
    parsed.running = flag != 0;
    if (!reader.read(kWideIntegerWidth, parsed.minimum)
        || !reader.read(kWideIntegerWidth, parsed.maximum)
        || !reader.read(kWideIntegerWidth, parsed.currentAtEpoch)
        || !reader.read(kWideIntegerWidth, parsed.remainingAtEpoch)
        || !reader.read(kWideIntegerWidth, parsed.epoch)) {
        return false;
    }
    std::uint64_t real32Bits = 0;
    if (!reader.read(kReal32Width, real32Bits)) {
        return false;
    }
    parsed.rate = std::bit_cast<float>(static_cast<std::uint32_t>(real32Bits));
    state = parsed;
    return true;
}

} // namespace

/** Advances the authored-object generation without reaching its signed terminal value. */
bool next_type4_generation(const Type4GenerationGuard& guard, std::int32_t& next) noexcept {
    return next_positive_generation(guard.hasLast, guard.last, next);
}

/** Encodes one package-owned entry with no caller-authored transform or child records. */
bool encode_type4(const Type4Preset& preset,
                  const Type4GenerationGuard& guard,
                  std::span<std::byte> output,
                  std::size_t& written) noexcept {
    written = 0;
    if (preset.generation <= 0 || preset.entryIndex < 0
        || (guard.hasLast && preset.generation <= guard.last) || output.size() < kType4ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType4ByteCount));
    const std::uint32_t generation =
        std::bit_cast<std::uint32_t>(preset.generation) + kSigned32Bias;
    const std::uint32_t entry = std::bit_cast<std::uint32_t>(preset.entryIndex) + kSigned32Bias;
    const std::uint32_t neutral = kSigned32Bias;
    const bool encoded =
        writer.write(generation, kSigned32Width) && writer.write(entry, kSigned32Width)
        && writer.write(preset.active ? 1U : 0U, kBoolWidth) && writer.write(0, kBoolWidth)
        && writer.write(neutral, kSigned32Width) && write_absent_client_ref(writer)
        && writer.write(0, kReal32Width) && writer.write(0, kReal32Width)
        && writer.write(0, kReal32Width) && writer.write(0, kBoolWidth) && writer.write(0, 2U);
    return encoded && writer.bit_count() == kType4BitCount && writer.finish(written)
           && written == kType4ByteCount;
}

/** Accepts only the canonical package-transform form produced by encode_type4. */
bool validate_type4_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    if (bitCount != kType4BitCount || input.size() != kType4ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t generation = 0;
    std::uint64_t entry = 0;
    std::uint64_t active = 0;
    std::uint64_t transformOverride = 0;
    std::uint64_t neutral = 0;
    std::uint64_t key = 0;
    std::uint64_t type = 0;
    std::uint64_t index = 0;
    std::uint64_t position = 0;
    std::uint64_t liveFlag = 0;
    std::uint64_t childCount = 0;
    return reader.read(kSigned32Width, generation) && reader.read(kSigned32Width, entry)
           && reader.read(kBoolWidth, active) && reader.read(kBoolWidth, transformOverride)
           && reader.read(kSigned32Width, neutral) && reader.read(kSigned32Width, key)
           && reader.read(kClientRefTypeWidth, type) && reader.read(kClientRefIndexWidth, index)
           && reader.read(kReal32Width, position) && position == 0
           && reader.read(kReal32Width, position) && position == 0
           && reader.read(kReal32Width, position) && position == 0
           && reader.read(kBoolWidth, liveFlag) && reader.read(2U, childCount)
           && generation > kSigned32Bias && entry >= kSigned32Bias && active <= 1U
           && transformOverride == 0 && neutral == kSigned32Bias && key == kClientRefAbsentKey
           && type == 0 && index == kClientRefIndexBias - 1U && liveFlag == 0 && childCount == 0
           && finish_padding(reader);
}

/** Finds the next positive type-23 sequence without wrapping. */
bool next_type23_sequence(const Type23SequenceGuard& guard,
                          Type23Channel channel,
                          std::int16_t& next) noexcept {
    next = 0;
    const std::size_t index = channel_index(channel);
    if (index == kType23ChannelCount || guard.last[index] < 0
        || guard.last[index] == std::numeric_limits<std::int16_t>::max()) {
        return false;
    }
    next = static_cast<std::int16_t>(guard.last[index] + 1);
    return next > 0;
}

/** Encodes one canonical type-23 auth body. */
bool encode_type23(const Type23Preset& preset,
                   const Type23SequenceGuard& guard,
                   std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    if (!valid_type23(preset, guard)) {
        return false;
    }
    Type23Body body{};
    const std::size_t selected = channel_index(preset.channel);
    body.channels[selected] = {preset.value, preset.sequence, preset.snap};
    return encode_type23_body(body, output, written);
}

/** Encodes one complete type-23 body. */
bool encode_type23_body(const Type23Body& body,
                        std::span<std::byte> output,
                        std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kType23ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType23ByteCount));
    return write_type23(writer, body) && writer.finish(written) && written == kType23ByteCount;
}

/** Decodes one complete type-23 body. */
bool decode_type23_body(std::span<const std::byte> input, Type23Body& body) noexcept {
    if (input.size() != kType23ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    Type23Body parsed{};
    if (!read_type23(reader, parsed) || !finish_padding(reader)) {
        return false;
    }
    body = parsed;
    return true;
}

/** Checks one packed type-23 override body. */
bool validate_type23_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    Type23Body body{};
    return bitCount == kType23BitCount && decode_type23_body(input, body);
}

/** Finds the next type-31 generation without reaching the reserved maximum value. */
bool next_type31_generation(const Type31GenerationGuard& guard, std::uint64_t& next) noexcept {
    next = 0;
    if (!guard.hasLast) {
        return true;
    }
    if (guard.last >= kReservedGeneration - 1) {
        return false;
    }
    next = guard.last + 1;
    return true;
}

/** Encodes one canonical type-31 configured-action pulse. */
bool encode_type31(const Type31Preset& preset,
                   const Type31GenerationGuard& guard,
                   std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    if (!valid_type31(preset, guard)) {
        return false;
    }
    return encode_type31_body({true, preset.generation, kUnusedAuxiliary}, output, written);
}

/** Encodes one complete type-31 body. */
bool encode_type31_body(const Type31Body& body,
                        std::span<std::byte> output,
                        std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kType31ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType31ByteCount));
    const bool encoded = writer.write(body.enabled ? kEnabled : 0U, kBoolWidth)
                         && writer.write(body.value, kWideIntegerWidth)
                         && writer.write(body.auxiliary, kWideIntegerWidth)
                         && writer.bit_count() == kType31BitCount;
    return encoded && writer.finish(written) && written == kType31ByteCount;
}

/** Decodes one complete type-31 body. */
bool decode_type31_body(std::span<const std::byte> input, Type31Body& body) noexcept {
    if (input.size() != kType31ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t enabled = 0;
    Type31Body parsed{};
    if (!reader.read(kBoolWidth, enabled) || !reader.read(kWideIntegerWidth, parsed.value)
        || !reader.read(kWideIntegerWidth, parsed.auxiliary) || !finish_padding(reader)) {
        return false;
    }
    parsed.enabled = enabled != 0;
    body = parsed;
    return true;
}

/** Checks one packed type-31 override body. */
bool validate_type31_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    Type31Body body{};
    return bitCount == kType31BitCount && decode_type31_body(input, body);
}

/** Encodes one complete type-18 body. */
bool encode_type18_body(const Type18Body& body,
                        std::span<std::byte> output,
                        std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kType18ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType18ByteCount));
    const std::uint32_t encodedValue = std::bit_cast<std::uint32_t>(body.tailValue) + kSigned32Bias;
    const bool encoded = write_shared_timed_state(writer, body.timed)
                         && writer.write(body.tailFlag ? 1U : 0U, kBoolWidth)
                         && writer.write(encodedValue, kSigned32Width)
                         && writer.bit_count() == kType18BitCount;
    return encoded && writer.finish(written) && written == kType18ByteCount;
}

/** Decodes one complete type-18 body. */
bool decode_type18_body(std::span<const std::byte> input, Type18Body& body) noexcept {
    if (input.size() != kType18ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    Type18Body parsed{};
    std::uint64_t flag = 0;
    std::uint64_t encodedValue = 0;
    if (!read_shared_timed_state(reader, parsed.timed) || !reader.read(kBoolWidth, flag)
        || !reader.read(kSigned32Width, encodedValue) || !finish_padding(reader)) {
        return false;
    }
    parsed.tailFlag = flag != 0;
    parsed.tailValue =
        std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(encodedValue) - kSigned32Bias);
    body = parsed;
    return true;
}

/** Checks one packed type-18 override body. */
bool validate_type18_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    Type18Body body{};
    return bitCount == kType18BitCount && decode_type18_body(input, body);
}

/** Encodes one complete type-35 countdown/timer body. */
bool encode_type35_body(const Type35Body& body,
                        std::span<std::byte> output,
                        std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kType35ByteCount || !valid_mode(body.mode0) || !valid_mode(body.mode1)) {
        return false;
    }
    bits::Writer writer(output.first(kType35ByteCount));
    const bool encoded =
        writer.write(body.flag0 ? 1U : 0U, kBoolWidth)
        && writer.write(body.flag1 ? 1U : 0U, kBoolWidth)
        && writer.write(static_cast<std::uint32_t>(static_cast<std::int32_t>(body.mode0)
                                                   + static_cast<std::int32_t>(kModeBias)),
                        kModeWidth)
        && writer.write(static_cast<std::uint32_t>(static_cast<std::int32_t>(body.mode1)
                                                   + static_cast<std::int32_t>(kModeBias)),
                        kModeWidth)
        && write_shared_timed_state(writer, body.timed) && writer.bit_count() == kType35BitCount;
    return encoded && writer.finish(written) && written == kType35ByteCount;
}

/** Decodes one complete type-35 countdown/timer body. */
bool decode_type35_body(std::span<const std::byte> input, Type35Body& body) noexcept {
    if (input.size() != kType35ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    Type35Body parsed{};
    std::uint64_t flag0 = 0;
    std::uint64_t flag1 = 0;
    std::uint64_t mode0 = 0;
    std::uint64_t mode1 = 0;
    if (!reader.read(kBoolWidth, flag0) || !reader.read(kBoolWidth, flag1)
        || !reader.read(kModeWidth, mode0) || !reader.read(kModeWidth, mode1)
        || !read_shared_timed_state(reader, parsed.timed) || !finish_padding(reader)) {
        return false;
    }
    parsed.flag0 = flag0 != 0;
    parsed.flag1 = flag1 != 0;
    parsed.mode0 = static_cast<std::int8_t>(static_cast<std::int32_t>(mode0)
                                            - static_cast<std::int32_t>(kModeBias));
    parsed.mode1 = static_cast<std::int8_t>(static_cast<std::int32_t>(mode1)
                                            - static_cast<std::int32_t>(kModeBias));
    body = parsed;
    return true;
}

/** Checks one packed type-35 override body. */
bool validate_type35_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    Type35Body body{};
    return bitCount == kType35BitCount && decode_type35_body(input, body);
}

/** Finds the next sequence revision while reserving 0xFF as inactive. */
bool next_type5_revision(const Type5RevisionGuard& guard, std::uint8_t& next) noexcept {
    next = 1U;
    if (!guard.hasLast) {
        return true;
    }
    if (guard.last >= kSequenceInactiveRevision - 1U) {
        return false;
    }
    next = static_cast<std::uint8_t>(guard.last + 1U);
    return next != kSequenceInactiveRevision;
}

/** Encodes the canonical sequence body: one revision and 129 unset ClientRefs. */
bool encode_type5(const Type5Preset& preset,
                  const Type5RevisionGuard& guard,
                  std::span<std::byte> output,
                  std::size_t& written) noexcept {
    written = 0;
    if (preset.revision == kSequenceInactiveRevision
        || (guard.hasLast && preset.revision <= guard.last) || output.size() < kType5ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType5ByteCount));
    bool encoded = writer.write(0, kWideIntegerWidth) && writer.write(0, kWideIntegerWidth)
                   && writer.write(preset.revision, 8U);
    for (std::size_t index = 0; encoded && index < 4U; ++index) {
        encoded = writer.write(0, kSigned32Width);
    }
    for (std::size_t index = 0; encoded && index < kSequenceReferenceCount; ++index) {
        encoded = write_absent_client_ref(writer);
    }
    encoded = encoded && write_absent_client_ref(writer);
    return encoded && writer.bit_count() == kType5BitCount && writer.finish(written)
           && written == kType5ByteCount;
}

/** Accepts only the canonical closed type-5 body emitted by encode_type5. */
bool validate_type5_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    if (bitCount != kType5BitCount || input.size() != kType5ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t revision = 0;
    if (!reader.read(kWideIntegerWidth, first) || !reader.read(kWideIntegerWidth, second)
        || !reader.read(8U, revision) || first != 0 || second != 0
        || revision == kSequenceInactiveRevision) {
        return false;
    }
    for (std::size_t index = 0; index < 4U; ++index) {
        std::uint64_t value = 0;
        if (!reader.read(kSigned32Width, value) || value != 0) {
            return false;
        }
    }
    for (std::size_t index = 0; index < kSequenceReferenceCount + 1U; ++index) {
        if (!read_absent_client_ref(reader)) {
            return false;
        }
    }
    return finish_padding(reader);
}

/** Finds the next cinematic generation without wrapping. */
bool next_type6_generation(const Type6GenerationGuard& guard, std::uint32_t& next) noexcept {
    next = 1U;
    if (!guard.hasLast) {
        return true;
    }
    if (guard.last == (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    next = guard.last + 1U;
    return next != 0;
}

/** Encodes the canonical cinematic start/stop body with empty participant arrays. */
bool encode_type6(const Type6Preset& preset,
                  const Type6GenerationGuard& guard,
                  std::span<std::byte> output,
                  std::size_t& written) noexcept {
    written = 0;
    if (preset.generation == 0 || (guard.hasLast && preset.generation <= guard.last)
        || output.size() < kType6ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType6ByteCount));
    bool encoded = writer.write(0, kWideIntegerWidth) && writer.write(0, kWideIntegerWidth)
                   && writer.write(preset.generation, kSigned32Width)
                   && writer.write(preset.active ? 1U : 0U, kBoolWidth)
                   && writer.write(0, kBoolWidth)
                   && write_absent_client_ref(writer)
                   // Bias three stores the native wildcard value -1 as wire value two.
                   && writer.write(2U, 6U)
                   // Both arrays are dynamically sized. Zero counts have no element payload.
                   && writer.write(0, 5U) && writer.write(0, 3U) && writer.write(0, kSigned32Width);
    return encoded && writer.bit_count() == kType6BitCount && writer.finish(written)
           && written == kType6ByteCount;
}

/** Accepts only the canonical closed type-6 body emitted by encode_type6. */
bool validate_type6_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    if (bitCount != kType6BitCount || input.size() != kType6ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t generation = 0;
    std::uint64_t active = 0;
    std::uint64_t force = 0;
    std::uint64_t role = 0;
    std::uint64_t count = 0;
    if (!reader.read(kWideIntegerWidth, first) || !reader.read(kWideIntegerWidth, second)
        || !reader.read(kSigned32Width, generation) || !reader.read(kBoolWidth, active)
        || !reader.read(kBoolWidth, force) || !read_absent_client_ref(reader)
        || !reader.read(6U, role) || !reader.read(5U, count) || first != 0 || second != 0
        || generation == 0 || force != 0 || role != 2U || count != 0) {
        return false;
    }
    if (!reader.read(3U, count) || count != 0) {
        return false;
    }
    std::uint64_t trailing = 0;
    return reader.read(kSigned32Width, trailing) && trailing == 0 && finish_padding(reader);
}

/** Advances the exact signed type-3 generation without wrapping its terminal value. */
bool next_type3_generation(const Type3GenerationGuard& guard, std::int32_t& next) noexcept {
    return next_positive_generation(guard.hasLast, guard.last, next);
}

/** Encodes the fixed 24-objective reset body and its monotonic generation. */
bool encode_type3(const Type3Body& body,
                  const Type3GenerationGuard& guard,
                  std::span<std::byte> output,
                  std::size_t& written) noexcept {
    written = 0;
    if (body.generation <= 0 || (guard.hasLast && body.generation <= guard.last)
        || output.size() < kType3ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType3ByteCount));
    bool encoded = writer.write(1U, kBoolWidth);
    for (const std::int8_t value : body.objectiveRevisions) {
        if (value < 0) {
            return false;
        }
        encoded = encoded && writer.write(1U, kBoolWidth)
                  && writer.write(static_cast<std::uint8_t>(value), kType3ValueWidth);
    }
    encoded = encoded && writer.write(1U, kBoolWidth)
              && writer.write(static_cast<std::uint32_t>(body.generation), kType3GenerationWidth);
    return encoded && writer.bit_count() == kType3BitCount && writer.finish(written)
           && written == kType3ByteCount;
}

/** Decodes one complete fixed-width type-3 objective reset body. */
bool decode_type3_body(std::span<const std::byte> input, Type3Body& body) noexcept {
    body = {};
    if (input.size() != kType3ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t arrayPresent = 0;
    if (!reader.read(kBoolWidth, arrayPresent) || arrayPresent == 0) {
        return false;
    }
    for (std::int8_t& value : body.objectiveRevisions) {
        std::uint64_t present = 0;
        std::uint64_t wire = 0;
        if (!reader.read(kBoolWidth, present)
            || (present != 0 && !reader.read(kType3ValueWidth, wire))) {
            return false;
        }
        value = present != 0 ? static_cast<std::int8_t>(wire) : 0;
    }
    std::uint64_t present = 0;
    std::uint64_t generation = 0;
    if (!reader.read(kBoolWidth, present)
        || (present != 0 && !reader.read(kType3GenerationWidth, generation))
        || !finish_padding(reader)) {
        return false;
    }
    body.generation = present != 0 ? static_cast<std::int32_t>(generation) : 0;
    return body.generation > 0;
}

/** Validates the exact type-3 bit count and canonical fixed-width body. */
bool validate_type3_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    Type3Body body{};
    return bitCount == kType3BitCount && decode_type3_body(input, body);
}

/** Finds the next positive task generation without wrapping. */
bool next_type38_generation(const Type38GenerationGuard& guard, std::int32_t& next) noexcept {
    return next_positive_generation(guard.hasLast, guard.last, next);
}

/** Encodes one exact signed-int32 task generation. */
bool encode_type38(const Type38Preset& preset,
                   const Type38GenerationGuard& guard,
                   std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    if (preset.generation <= 0 || (guard.hasLast && preset.generation <= guard.last)
        || output.size() < kType38ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType38ByteCount));
    const std::uint32_t wire = std::bit_cast<std::uint32_t>(preset.generation) + kSigned32Bias;
    return writer.write(wire, kSigned32Width) && writer.bit_count() == kType38BitCount
           && writer.finish(written) && written == kType38ByteCount;
}

/** Decodes one exact signed-int32 task generation. */
bool decode_type38_body(std::span<const std::byte> input, std::int32_t& generation) noexcept {
    if (input.size() != kType38ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t wire = 0;
    if (!reader.read(kSigned32Width, wire) || !finish_padding(reader)) {
        return false;
    }
    generation = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(wire) - kSigned32Bias);
    return generation > 0;
}

/** Checks one packed type-38 task generation. */
bool validate_type38_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    std::int32_t generation = 0;
    return bitCount == kType38BitCount && decode_type38_body(input, generation);
}

/** Finds the next positive dialogue fire sequence without wrapping. */
bool next_type53_sequence(const Type53SequenceGuard& guard,
                          std::uint16_t cueIndex,
                          std::int32_t& next) noexcept {
    next = 0;
    if (cueIndex >= guard.last.size() || guard.last[cueIndex] < 0
        || guard.last[cueIndex] == (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    next = guard.last[cueIndex] + 1;
    return next > 0;
}

/** Encodes one dialogue pulse with the schema's optional world field present only for its cue. */
bool encode_type53(const Type53Preset& preset,
                   const Type53SequenceGuard& guard,
                   std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    if (preset.cueIndex >= kType53EntryCount || preset.sequence <= 0
        || preset.sequence <= guard.last[preset.cueIndex] || output.size() < kType53ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType53ByteCount));
    bool encoded = write_absent_client_ref(writer);
    for (std::size_t index = 0; encoded && index < kType53EntryCount; ++index) {
        const bool selected = index == preset.cueIndex;
        const std::int32_t sequence = selected ? preset.sequence : guard.last[index];
        const std::uint32_t wireSequence = std::bit_cast<std::uint32_t>(sequence) + kSigned32Bias;
        const std::int8_t mode = selected ? kDialogueFireMode : kDialogueInactiveMode;
        encoded =
            writer.write(0, kWideIntegerWidth) && writer.write(selected ? 1U : 0U, kBoolWidth);
        if (encoded && selected) {
            encoded = writer.write(kDialogueActiveWorld, kWideIntegerWidth);
        }
        encoded =
            encoded && write_absent_client_ref(writer) && writer.write(wireSequence, kSigned32Width)
            && writer.write(static_cast<std::uint32_t>(static_cast<std::int32_t>(mode) + kModeBias),
                            kModeWidth);
    }
    return encoded && writer.bit_count() == kType53BitCount && writer.finish(written)
           && written == kType53ByteCount;
}

/** Validates the closed pulse form rather than accepting arbitrary type-53 references. */
bool validate_type53_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    if (bitCount != kType53BitCount || input.size() != kType53ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    if (!read_absent_client_ref(reader)) {
        return false;
    }
    std::size_t activeCount = 0;
    for (std::size_t index = 0; index < kType53EntryCount; ++index) {
        std::uint64_t tick = 0;
        std::uint64_t hasWorld = 0;
        std::uint64_t world = 0;
        std::uint64_t wireSequence = 0;
        std::uint64_t wireMode = 0;
        if (!reader.read(kWideIntegerWidth, tick) || !reader.read(kBoolWidth, hasWorld)
            || (hasWorld != 0 && !reader.read(kWideIntegerWidth, world))
            || !read_absent_client_ref(reader) || !reader.read(kSigned32Width, wireSequence)
            || !reader.read(kModeWidth, wireMode)) {
            return false;
        }
        const std::int32_t sequence =
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(wireSequence) - kSigned32Bias);
        const std::int32_t mode = static_cast<std::int32_t>(wireMode) - kModeBias;
        if (tick != 0) {
            return false;
        }
        if (hasWorld != 0) {
            if (hasWorld != 1 || world != kDialogueActiveWorld || sequence <= 0
                || mode != kDialogueFireMode || ++activeCount != 1) {
                return false;
            }
        } else if (mode != kDialogueInactiveMode) {
            return false;
        }
    }
    return activeCount == 1 && finish_padding(reader);
}

/** Encodes one complete three-lane type-68 directive state. */
bool encode_type68(const Type68Preset& preset,
                   std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    if ((preset.audience.slotIndex >= 0 && (preset.audience.slotType != 70
            || preset.audience.registryKey == 0 || preset.audience.registryKey == kClientRefAbsentKey))
        || output.size() < kType68ByteCount || preset.state < 0 || preset.state > 2
        || (preset.visible
            && (preset.nameHash == 0 || preset.nameHash == kClientRefAbsentKey
                || preset.elementIndex < 0))
        || (preset.navpoint.slotIndex >= 0 && (!preset.visible || preset.state != 0
            || preset.navpoint.slotType != 47 || preset.navpoint.registryKey == 0
            || preset.navpoint.registryKey == kClientRefAbsentKey))) {
        return false;
    }
    bits::Writer writer(output.first(kType68ByteCount));
    bool encoded = (writer.write(preset.audience.registryKey, 32)
        && writer.write(static_cast<std::uint32_t>(preset.audience.slotType) + 1U, 7)
        && writer.write(static_cast<std::uint32_t>(preset.audience.slotIndex) + kSigned16Bias, 16)) && write_absent_client_ref(writer);
    for (std::size_t index = 0; encoded && index < kType68EntryCount; ++index) {
        encoded =
            index == 0 && preset.visible
                ? write_directive_entry(writer, preset.nameHash, preset.elementIndex, preset.state, preset.navpoint)
                : write_directive_entry(writer, kClientRefAbsentKey, 0, -1);
    }
    encoded = encoded && writer.write(preset.visible ? 1U : 0U, kDirectiveActiveIndexWidth);
    return encoded && writer.bit_count() == kType68BitCount && writer.finish(written)
           && written == kType68ByteCount;
}

/** Validates the crash-bearing selector fields and the exact type-68 body shape. */
bool validate_type68_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    if (bitCount != kType68BitCount || input.size() != kType68ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    auto skip = [&reader](std::size_t count) noexcept {
        std::uint64_t discarded = 0;
        while (count != 0) {
            const auto width = static_cast<std::uint8_t>((std::min)(count, std::size_t{64}));
            if (!reader.read(width, discarded)) {
                return false;
            }
            count -= width;
        }
        return true;
    };
    if (!skip(110)) {
        return false;
    }
    std::array<std::uint32_t, kType68EntryCount> names{};
    std::array<std::int32_t, kType68EntryCount> elements{};
    std::array<std::int32_t, kType68EntryCount> states{};
    for (std::size_t index = 0; index < kType68EntryCount; ++index) {
        std::uint64_t name = 0;
        std::uint64_t element = 0;
        std::uint64_t state = 0;
        if (!reader.read(32, name) || !reader.read(32, element)
            || !reader.read(kDirectiveStateWidth, state) || !skip(1'497)) {
            return false;
        }
        names[index] = static_cast<std::uint32_t>(name);
        elements[index] =
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(element) - kSigned32Bias);
        states[index] = static_cast<std::int32_t>(state) - 1;
    }
    std::uint64_t activeWire = 0;
    if (!reader.read(kDirectiveActiveIndexWidth, activeWire)) {
        return false;
    }
    const std::int32_t active = static_cast<std::int32_t>(activeWire) - 1;
    if (active < -1 || active >= static_cast<std::int32_t>(kType68EntryCount)) {
        return false;
    }
    if (active >= 0) {
        const std::size_t selected = static_cast<std::size_t>(active);
        if (names[selected] == 0 || names[selected] == kClientRefAbsentKey || elements[selected] < 0
            || states[selected] < 0 || states[selected] > 2) {
            return false;
        }
    }
    return finish_padding(reader);
}

/** Encodes the no-filter type-70 encounter engagement state. */
bool encode_type70(const Type70Preset& preset,
                   std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    if (preset.flags > 0x1FU || output.size() < kType70ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType70ByteCount));
    const auto revision = static_cast<std::uint16_t>(static_cast<std::int32_t>(preset.revision)
                                                     + static_cast<std::int32_t>(kSigned16Bias));
    const bool encoded = writer.write(preset.flags, 5U)
                         // Both dynamic collections are absent; no element payload follows.
                         && writer.write(0, kBoolWidth) && writer.write(revision, 16U)
                         && writer.write(0, kBoolWidth);
    return encoded && writer.bit_count() == kType70BitCount && writer.finish(written)
           && written == kType70ByteCount;
}

/** Validates the exact no-filter type-70 form while preserving all five flag bits. */
bool validate_type70_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    if (bitCount != kType70BitCount || input.size() != kType70ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t flags = 0;
    std::uint64_t playerListPresent = 0;
    std::uint64_t revision = 0;
    std::uint64_t definitionListPresent = 0;
    return reader.read(5U, flags) && flags <= 0x1FU && reader.read(kBoolWidth, playerListPresent)
           && playerListPresent == 0 && reader.read(16U, revision)
           && reader.read(kBoolWidth, definitionListPresent) && definitionListPresent == 0
           && finish_padding(reader);
}

bool next_type42_generation(const Type42GenerationGuard& guard, std::int32_t& next) noexcept {
    return next_positive_generation(guard.hasLast, guard.last, next);
}

/** The client treats the FNV-1 basis as "no performance", so neither it nor zero may be sent. */
[[nodiscard]] bool valid_type42_name(std::uint32_t nameHash) noexcept {
    return nameHash != 0 && nameHash != kClientRefAbsentKey;
}

/** Refuses a generation that does not rise, because the client starts the state only once. */
bool encode_type42(const Type42Preset& preset,
                   const Type42GenerationGuard& guard,
                   std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    if (!valid_type42_name(preset.nameHash) || preset.generation <= 0
        || (guard.hasLast && preset.generation <= guard.last) || output.size() < kType42ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType42ByteCount));
    const std::uint32_t generation =
        std::bit_cast<std::uint32_t>(preset.generation) + kSigned32Bias;
    const bool encoded = writer.write(kEnabled, kBoolWidth) && writer.write(preset.nameHash, 32U)
                         && writer.write(preset.value, 32U)
                         && writer.write(generation, kSigned32Width)
                         // The row table is absent; the client keeps its own rows.
                         && writer.write(0, kBoolWidth);
    return encoded && writer.bit_count() == kType42BitCount && writer.finish(written)
           && written == kType42ByteCount;
}

/** Accepts only the command group present, a real state name, and an absent row table. */
bool validate_type42_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    if (bitCount != kType42BitCount || input.size() != kType42ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t commandPresent = 0;
    std::uint64_t nameHash = 0;
    std::uint64_t value = 0;
    std::uint64_t generation = 0;
    std::uint64_t rowsPresent = 0;
    return reader.read(kBoolWidth, commandPresent) && commandPresent == kEnabled
           && reader.read(32U, nameHash) && valid_type42_name(static_cast<std::uint32_t>(nameHash))
           && reader.read(32U, value) && reader.read(kSigned32Width, generation)
           && generation > kSigned32Bias && reader.read(kBoolWidth, rowsPresent) && rowsPresent == 0
           && finish_padding(reader);
}

/** @return True when the area reference names a real slot the client evaluator can resolve. */
[[nodiscard]] bool valid_type71_area(const Type71Body& body) noexcept {
    return body.areaRegistryKey != 0 && body.areaRegistryKey != kClientRefAbsentKey
           && body.areaSlotType < (std::uint8_t{1} << kClientRefTypeWidth) - 1U
           && body.areaSlotIndex < kClientRefIndexBias;
}

/** Encodes the fixed 183-bit type-71 body. */
bool encode_type71(const Type71Body& body,
                   std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    if (!valid_type71_area(body) || !std::isfinite(body.leaveSeconds) || body.leaveSeconds < 0.0F
        || output.size() < kType71ByteCount) {
        return false;
    }
    bits::Writer writer(output.first(kType71ByteCount));
    const std::uint32_t state = std::bit_cast<std::uint32_t>(body.state) + kSigned32Bias;
    const bool encoded =
        writer.write(state, kSigned32Width) && writer.write(body.playerIdentity, kWideIntegerWidth)
        && writer.write(body.areaRegistryKey, 32)
        && writer.write(std::uint32_t{body.areaSlotType} + 1U, kClientRefTypeWidth)
        && writer.write(std::uint32_t{body.areaSlotIndex} + kClientRefIndexBias,
                        kClientRefIndexWidth)
        && writer.write(std::bit_cast<std::uint32_t>(body.leaveSeconds), kReal32Width);
    return encoded && writer.bit_count() == kType71BitCount && writer.finish(written)
           && written == kType71ByteCount;
}

/** Decodes one complete type-71 body. */
bool decode_type71_body(std::span<const std::byte> input, Type71Body& body) noexcept {
    body = {};
    if (input.size() != kType71ByteCount) {
        return false;
    }
    bits::Reader reader(input);
    std::uint64_t state = 0;
    std::uint64_t identity = 0;
    std::uint64_t key = 0;
    std::uint64_t type = 0;
    std::uint64_t index = 0;
    std::uint64_t seconds = 0;
    if (!reader.read(kSigned32Width, state) || !reader.read(kWideIntegerWidth, identity)
        || !reader.read(32, key) || !reader.read(kClientRefTypeWidth, type)
        || !reader.read(kClientRefIndexWidth, index) || !reader.read(kReal32Width, seconds)
        || !finish_padding(reader) || type == 0 || index < kClientRefIndexBias) {
        return false;
    }
    Type71Body parsed{};
    parsed.state = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(state) - kSigned32Bias);
    parsed.playerIdentity = identity;
    parsed.areaRegistryKey = static_cast<std::uint32_t>(key);
    parsed.areaSlotType = static_cast<std::uint8_t>(type - 1U);
    parsed.areaSlotIndex = static_cast<std::uint16_t>(index - kClientRefIndexBias);
    parsed.leaveSeconds = std::bit_cast<float>(static_cast<std::uint32_t>(seconds));
    if (!valid_type71_area(parsed) || !std::isfinite(parsed.leaveSeconds)
        || parsed.leaveSeconds < 0.0F) {
        return false;
    }
    body = parsed;
    return true;
}

/** Validates the exact type-71 body shape and its present area reference. */
bool validate_type71_body(std::span<const std::byte> input, std::size_t bitCount) noexcept {
    Type71Body body{};
    return bitCount == kType71BitCount && decode_type71_body(input, body);
}

} // namespace sunrise::middleware::bap::activity_message::scriptable_auth
