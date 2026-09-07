#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::gameplay::external {

/** The five-bit lane list is bounded before any event body is retained. */
inline constexpr std::size_t kSimulationEventCapacity = 32;
/** The payload arena cannot exceed one plaintext DTLS record. */
inline constexpr std::size_t kSimulationEventLaneByteCapacity = 1'400;
/** The stock registry owns selectors 0 through 21. */
inline constexpr std::uint8_t kMaximumSimulationEventType = 21;
/** A single body cannot exceed the complete lane arena. */
inline constexpr std::size_t kMaximumSimulationEventPayloadBits =
    kSimulationEventLaneByteCapacity * 8U;

/** Identifies the optional selector body or the required event body. */
enum class SimulationEventPayloadPart : std::uint8_t { primary, secondary };

/** Exact validated body bits retained in the owning batch arena. */
struct SimulationEventPayload final {
    std::uint16_t bitOffset{};
    std::uint16_t bitCount{};
};

/** One event owns two slices but no payload storage. */
struct SimulationEventRecord final {
    SimulationEventPayload primary{};
    SimulationEventPayload secondary{};
    std::uint8_t eventType{};
    bool primaryPresent{};
};

/** One complete lane-0 list and its single DTLS-bounded raw body arena. */
struct SimulationEventBatch final {
    std::array<SimulationEventRecord, kSimulationEventCapacity> records{};
    std::array<std::byte, kSimulationEventLaneByteCapacity> arena{};
    std::uint16_t arenaBitCount{};
    std::uint8_t count{};
};

/** The primary-body dependency remains valid for one codec call. */
struct SimulationEventPayloadView final {
    std::span<const std::byte> arena{};
    SimulationEventPayload payload{};
};

/** A schema validator consumes one body without retaining source storage. */
using ReadSimulationEventPayload = bool (*)(const void* context,
                                            std::uint8_t eventType,
                                            SimulationEventPayloadPart part,
                                            const SimulationEventPayloadView* primary,
                                            encoding::bits::Reader& reader) noexcept;

/** A validator consumes exactly one reflected event body. */
struct SimulationEventPayloadCodec final {
    const void* context{};
    ReadSimulationEventPayload read{};
    std::size_t maximumPrimaryBits{};
    std::size_t maximumSecondaryBits{};
};

/** Appends raw bits to the arena. The batch is unchanged when they do not fit. */
[[nodiscard]] bool append_simulation_event_payload(SimulationEventBatch& batch,
                                                   std::span<const std::byte> bytes,
                                                   std::size_t bitCount,
                                                   SimulationEventPayload& output) noexcept;

/** Positions a reader at one retained slice. */
[[nodiscard]] bool simulation_event_payload_reader(const SimulationEventBatch& batch,
                                                   const SimulationEventPayload& payload,
                                                   encoding::bits::Reader& output) noexcept;

/** Captures a complete lane only after every event body validates. */
[[nodiscard]] bool read_simulation_event_lane(encoding::bits::Reader& reader,
                                              const SimulationEventPayloadCodec& codec,
                                              SimulationEventBatch& output) noexcept;

/** Validates every retained slice before writing any lane bits. */
[[nodiscard]] bool write_simulation_event_lane(encoding::bits::Writer& writer,
                                               const SimulationEventPayloadCodec& codec,
                                               const SimulationEventBatch& batch) noexcept;

/** A lane adapter consumes the complete outer list and terminator. */
using ReadLane0 = bool (*)(const void* context, encoding::bits::Reader& reader) noexcept;
/** A lane adapter writes the complete outer list and terminator. */
using WriteLane0 = bool (*)(const void* context, encoding::bits::Writer& writer) noexcept;

/** Null callbacks admit or emit only the absent-list bit. */
struct Lane0Codec final {
    const void* context{};
    ReadLane0 read{};
    WriteLane0 write{};
};

/** Delegates a complete lane read, or accepts one literal absent bit. */
[[nodiscard]] bool read_lane0(encoding::bits::Reader& reader, const Lane0Codec& codec) noexcept;

/** Delegates a complete lane write, or emits one literal absent bit. */
[[nodiscard]] bool write_lane0(encoding::bits::Writer& writer, const Lane0Codec& codec) noexcept;

} // namespace sunrise::middleware::gameplay::external
