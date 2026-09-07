#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_reader.h"

namespace sunrise::middleware::gameplay::external {

/** The control view owns 32 fixed-size retained slots. */
inline constexpr std::size_t kControlStateSlotCount = 32;
/** One decoded native control state occupies 160 bytes. */
inline constexpr std::size_t kControlStateByteCount = 160;
/** A payload cannot consume more than one plaintext gameplay packet. */
inline constexpr std::size_t kMaximumControlStatePayloadBits = 1'400U * 8U;

/** Proved scalar fields keep native offsets; the lead vector is validated but not materialized. */
struct ControlState final {
    std::array<std::byte, kControlStateByteCount> bytes{};
};

/** One indexed lane row and its optional replacement state. */
struct ControlStateRow final {
    ControlState state{};
    std::uint8_t index{};
    bool payloadPresent{};
};

/** One complete bounded control lane. */
struct ControlStateBatch final {
    std::array<ControlStateRow, kControlStateSlotCount> rows{};
    std::uint8_t count{};
};

/** Decodes one variable control-state payload into its 160-byte native form. */
using ReadControlStatePayload = bool (*)(const void* context,
                                         std::uint8_t index,
                                         encoding::bits::Reader& reader,
                                         ControlState& output) noexcept;

/** The payload reader supplies the exact nested control-state codec. */
struct ControlStatePayloadCodec final {
    const void* context{};
    ReadControlStatePayload read{};
    std::size_t maximumPayloadBits{};
};

/** Decodes the complete fixed grammar and retains its proved native scalar fields. */
[[nodiscard]] bool read_control_state_payload(encoding::bits::Reader& reader,
                                              ControlState& output) noexcept;

/** Returns the stock control-state payload adapter. */
[[nodiscard]] ControlStatePayloadCodec control_state_payload_codec() noexcept;

/** Reads one complete lane and commits no reader or output state on failure. */
[[nodiscard]] bool read_control_state_lane(encoding::bits::Reader& reader,
                                           const ControlStatePayloadCodec& codec,
                                           ControlStateBatch& output) noexcept;

/** A lane adapter consumes the complete outer list and terminator. */
using ReadLane1 = bool (*)(const void* context, encoding::bits::Reader& reader) noexcept;

/** Null callbacks admit only the absent-list bit. */
struct Lane1Codec final {
    const void* context{};
    ReadLane1 read{};
};

/** Delegates a complete lane read, or accepts one literal absent bit. */
[[nodiscard]] bool read_lane1(encoding::bits::Reader& reader, const Lane1Codec& codec) noexcept;

} // namespace sunrise::middleware::gameplay::external
