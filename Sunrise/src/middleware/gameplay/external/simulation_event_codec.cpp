#include "simulation_event_codec.h"

#include <memory>
#include <utility>

#include "../../encoding/bit_raw.h"

namespace sunrise::middleware::gameplay::external {
namespace {

/** The list terminator and the primary-present marker are one bit each. */
constexpr std::uint8_t kLaneFlagWidth = 1;
/** The stock event selector is five bits. */
constexpr std::uint8_t kEventTypeWidth = 5;
/** Bits in one byte, used to round the arena's bit count up to whole bytes. */
constexpr std::size_t kByteBits = 8;

/** @return Whole bytes holding one arena's meaningful bits. */
[[nodiscard]] std::size_t arena_bytes(const SimulationEventBatch& batch) noexcept {
    return (static_cast<std::size_t>(batch.arenaBitCount) + kByteBits - 1U) / kByteBits;
}

/** Selects the caller's per-part validation ceiling. */
[[nodiscard]] std::size_t payload_limit(const SimulationEventPayloadCodec& codec,
                                        SimulationEventPayloadPart part) noexcept {
    return part == SimulationEventPayloadPart::primary ? codec.maximumPrimaryBits
                                                       : codec.maximumSecondaryBits;
}

/** Rejects slices that escape the initialized arena bits. */
[[nodiscard]] bool valid_payload(const SimulationEventBatch& batch,
                                 const SimulationEventPayload& payload) noexcept {
    return payload.bitOffset <= batch.arenaBitCount
           && payload.bitCount <= batch.arenaBitCount - payload.bitOffset;
}

/** Copies an unaligned slice without borrowing its source storage. */
[[nodiscard]] bool copy_bits(encoding::bits::Reader source,
                             std::size_t bitCount,
                             encoding::bits::Writer& output) noexcept {
    return encoding::bits::copy(source, output, bitCount);
}

/** Builds the dependency view used while validating a secondary body. */
[[nodiscard]] SimulationEventPayloadView view(const SimulationEventBatch& batch,
                                              const SimulationEventPayload& payload) noexcept {
    return {std::span(batch.arena).first(arena_bytes(batch)), payload};
}

/** Replays one retained slice through its SDK-driven validator. */
[[nodiscard]] bool validate_payload(const SimulationEventPayloadCodec& codec,
                                    const SimulationEventBatch& batch,
                                    const SimulationEventRecord& record,
                                    SimulationEventPayloadPart part) noexcept {
    const SimulationEventPayload& payload =
        part == SimulationEventPayloadPart::primary ? record.primary : record.secondary;
    if (codec.read == nullptr || !valid_payload(batch, payload)
        || payload.bitCount > payload_limit(codec, part)) {
        return false;
    }
    encoding::bits::Reader reader({});
    if (!simulation_event_payload_reader(batch, payload, reader)) {
        return false;
    }
    SimulationEventPayloadView primary{};
    const SimulationEventPayloadView* primaryPointer = nullptr;
    if (part == SimulationEventPayloadPart::secondary && record.primaryPresent) {
        primary = view(batch, record.primary);
        primaryPointer = &primary;
    }
    const std::size_t before = reader.remaining_bits();
    return codec.read(codec.context, record.eventType, part, primaryPointer, reader)
           && before >= reader.remaining_bits()
           && before - reader.remaining_bits() == payload.bitCount;
}

/** Validates one source body before appending its exact consumed bits. */
[[nodiscard]] bool capture_payload(const SimulationEventPayloadCodec& codec,
                                   SimulationEventBatch& batch,
                                   std::uint8_t eventType,
                                   SimulationEventPayloadPart part,
                                   const SimulationEventPayloadView* primary,
                                   encoding::bits::Reader& reader,
                                   SimulationEventPayload& output) noexcept {
    if (codec.read == nullptr || payload_limit(codec, part) > kMaximumSimulationEventPayloadBits) {
        return false;
    }
    encoding::bits::Reader start = reader;
    const std::size_t before = reader.remaining_bits();
    if (!codec.read(codec.context, eventType, part, primary, reader)
        || reader.remaining_bits() > before) {
        return false;
    }
    const std::size_t consumed = before - reader.remaining_bits();
    if (consumed > payload_limit(codec, part)) {
        return false;
    }
    std::array<std::byte, kSimulationEventLaneByteCapacity> body{};
    encoding::bits::Writer writer(body);
    std::size_t written = 0;
    if (!copy_bits(start, consumed, writer) || !writer.finish(written)) {
        return false;
    }
    return append_simulation_event_payload(batch, std::span(body).first(written), consumed, output);
}

} // namespace

/** Appends raw bits transactionally so failed writes leave the batch unchanged. */
bool append_simulation_event_payload(SimulationEventBatch& batch,
                                     std::span<const std::byte> bytes,
                                     std::size_t bitCount,
                                     SimulationEventPayload& output) noexcept {
    if (bitCount > bytes.size() * kByteBits || batch.arenaBitCount > batch.arena.size() * kByteBits
        || bitCount > batch.arena.size() * kByteBits - batch.arenaBitCount) {
        return false;
    }
    auto arena = batch.arena;
    encoding::bits::Writer writer(arena);
    encoding::bits::Reader existing(std::span(batch.arena).first(arena_bytes(batch)));
    encoding::bits::Reader source(bytes);
    if (!copy_bits(existing, batch.arenaBitCount, writer) || !copy_bits(source, bitCount, writer)) {
        return false;
    }
    std::size_t written = 0;
    if (!writer.finish(written)) {
        return false;
    }
    const std::size_t offset = batch.arenaBitCount;
    batch.arena = arena;
    batch.arenaBitCount = static_cast<std::uint16_t>(offset + bitCount);
    output = {static_cast<std::uint16_t>(offset), static_cast<std::uint16_t>(bitCount)};
    return true;
}

/** Positions a reader at one slice while retaining the arena's trailing bits. */
bool simulation_event_payload_reader(const SimulationEventBatch& batch,
                                     const SimulationEventPayload& payload,
                                     encoding::bits::Reader& output) noexcept {
    if (!valid_payload(batch, payload)) {
        return false;
    }
    encoding::bits::Reader reader(std::span(batch.arena).first(arena_bytes(batch)));
    if (!reader.skip(payload.bitOffset)) {
        return false;
    }
    output = reader;
    return true;
}

/** Captures a complete lane only after every event body validates. */
bool read_simulation_event_lane(encoding::bits::Reader& reader,
                                const SimulationEventPayloadCodec& codec,
                                SimulationEventBatch& output) noexcept {
    encoding::bits::Reader candidateReader = reader;
    const auto candidate = std::make_unique<SimulationEventBatch>();
    if (candidate == nullptr) {
        return false;
    }
    while (true) {
        std::uint64_t present = 0;
        if (!candidateReader.read(kLaneFlagWidth, present)) {
            return false;
        }
        if (present == 0) {
            reader = candidateReader;
            output = *candidate;
            return true;
        }
        if (candidate->count == candidate->records.size()) {
            return false;
        }
        std::uint64_t eventType = 0;
        std::uint64_t primaryPresent = 0;
        if (!candidateReader.read(kEventTypeWidth, eventType)
            || eventType > kMaximumSimulationEventType
            || !candidateReader.read(kLaneFlagWidth, primaryPresent)) {
            return false;
        }
        SimulationEventRecord& record = candidate->records[candidate->count];
        record.eventType = static_cast<std::uint8_t>(eventType);
        record.primaryPresent = primaryPresent != 0;
        if (record.primaryPresent
            && !capture_payload(codec,
                                *candidate,
                                record.eventType,
                                SimulationEventPayloadPart::primary,
                                nullptr,
                                candidateReader,
                                record.primary)) {
            return false;
        }
        SimulationEventPayloadView primary{};
        const SimulationEventPayloadView* primaryPointer = nullptr;
        if (record.primaryPresent) {
            primary = view(*candidate, record.primary);
            primaryPointer = &primary;
        }
        if (!capture_payload(codec,
                             *candidate,
                             record.eventType,
                             SimulationEventPayloadPart::secondary,
                             primaryPointer,
                             candidateReader,
                             record.secondary)) {
            return false;
        }
        ++candidate->count;
    }
}

/** Validates every retained slice before writing any lane bits. */
bool write_simulation_event_lane(encoding::bits::Writer& writer,
                                 const SimulationEventPayloadCodec& codec,
                                 const SimulationEventBatch& batch) noexcept {
    if (batch.count > batch.records.size()
        || batch.arenaBitCount > batch.arena.size() * kByteBits) {
        return false;
    }
    for (std::size_t index = 0; index < batch.count; ++index) {
        const SimulationEventRecord& record = batch.records[index];
        if (record.eventType > kMaximumSimulationEventType
            || (!record.primaryPresent && record.primary.bitCount != 0)
            || (record.primaryPresent
                && !validate_payload(codec, batch, record, SimulationEventPayloadPart::primary))
            || !validate_payload(codec, batch, record, SimulationEventPayloadPart::secondary)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < batch.count; ++index) {
        const SimulationEventRecord& record = batch.records[index];
        if (!writer.write(1, kLaneFlagWidth) || !writer.write(record.eventType, kEventTypeWidth)
            || !writer.write(record.primaryPresent ? 1U : 0U, kLaneFlagWidth)) {
            return false;
        }
        for (const auto [part, payload] :
             {std::pair{SimulationEventPayloadPart::primary, record.primary},
              std::pair{SimulationEventPayloadPart::secondary, record.secondary}}) {
            if (part == SimulationEventPayloadPart::primary && !record.primaryPresent) {
                continue;
            }
            encoding::bits::Reader source({});
            if (!simulation_event_payload_reader(batch, payload, source)
                || !copy_bits(source, payload.bitCount, writer)) {
                return false;
            }
        }
    }
    return writer.write(0, kLaneFlagWidth);
}

/** Delegates a complete lane read or accepts one literal absent bit. */
bool read_lane0(encoding::bits::Reader& reader, const Lane0Codec& codec) noexcept {
    if (codec.read != nullptr) {
        return codec.read(codec.context, reader);
    }
    encoding::bits::Reader candidate = reader;
    std::uint64_t present = 0;
    if (!candidate.read(kLaneFlagWidth, present) || present != 0) {
        return false;
    }
    reader = candidate;
    return true;
}

/** Delegates a complete lane write or emits one literal absent bit. */
bool write_lane0(encoding::bits::Writer& writer, const Lane0Codec& codec) noexcept {
    return codec.write != nullptr ? codec.write(codec.context, writer)
                                  : writer.write(0, kLaneFlagWidth);
}

} // namespace sunrise::middleware::gameplay::external
