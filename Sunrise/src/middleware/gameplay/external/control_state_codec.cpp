#include "control_state_codec.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace sunrise::middleware::gameplay::external {
namespace {

/** Lane rows use a presence bit, a five-bit index and a payload-presence bit. */
constexpr std::uint8_t kLaneFlagWidth = 1;
constexpr std::uint8_t kControlIndexWidth = 5;
/** The longest stock control payload is below this local bound. */
constexpr std::size_t kStockControlStatePayloadBits = 256;

/** Stores one native scalar without relying on the byte arena's alignment. */
template <typename Value>
void store(ControlState& state, std::size_t offset, Value value) noexcept {
    static_assert(std::is_trivially_copyable_v<Value>);
    std::memcpy(state.bytes.data() + offset, &value, sizeof(value));
}

/** Reads one raw field and narrows it only after the complete width is present. */
template <typename Value>
[[nodiscard]] bool
read_raw(encoding::bits::Reader& reader, std::uint8_t width, Value& output) noexcept {
    std::uint64_t raw = 0;
    if (!reader.read(width, raw)) {
        return false;
    }
    output = static_cast<Value>(raw);
    return true;
}

/** Decodes the finite endpoint-preserving quantized real used by control state. */
[[nodiscard]] bool read_real(encoding::bits::Reader& reader,
                             std::uint8_t width,
                             float minimum,
                             float maximum,
                             float& output) noexcept {
    std::uint64_t raw = 0;
    if (!reader.read(width, raw)) {
        return false;
    }
    const std::uint64_t levels = std::uint64_t{1} << width;
    if (raw == 0) {
        output = minimum;
    } else if (raw == levels - 1U) {
        output = maximum;
    } else {
        const float step = (maximum - minimum) / static_cast<float>(levels - 2U);
        output = minimum + (static_cast<float>(raw - 1U) + 0.5F) * step;
    }
    return std::isfinite(output);
}

/** Reads the two fixed reflected roots used by both optional control references. */
[[nodiscard]] bool read_reference_pair(encoding::bits::Reader& reader,
                                       ControlState& state,
                                       std::size_t offset) noexcept {
    bool pairPresent = false;
    if (!read_raw(reader, 1, pairPresent)) {
        return false;
    }
    store(state, offset + 12U, pairPresent);
    if (!pairPresent) {
        return true;
    }
    bool entityPresent = false;
    if (!read_raw(reader, 1, entityPresent)) {
        return false;
    }
    std::uint32_t entity = 0;
    if (entityPresent) {
        std::uint16_t slot = 0;
        std::uint8_t incarnation = 0;
        if (!read_raw(reader, 13, slot) || !read_raw(reader, 4, incarnation)) {
            return false;
        }
        entity =
            static_cast<std::uint32_t>(slot) | (static_cast<std::uint32_t>(incarnation) << 16U);
    } else {
        bool secondSentinel = false;
        if (!read_raw(reader, 1, secondSentinel)) {
            return false;
        }
        entity = secondSentinel ? std::numeric_limits<std::uint32_t>::max() - 1U
                                : std::numeric_limits<std::uint32_t>::max();
    }
    std::uint32_t valueRaw = 0;
    if (!read_raw(reader, 32, valueRaw)) {
        return false;
    }
    const std::int32_t value = static_cast<std::int32_t>(valueRaw ^ 0x80000000U);
    store(state, offset, entity);
    store(state, offset + 8U, value);
    return true;
}

/** Reads the nested primary aim block, including its optional reference and lead vector. */
[[nodiscard]] bool
read_primary_aim(encoding::bits::Reader& reader, ControlState& state, bool& present) noexcept {
    if (!read_raw(reader, 1, present)) {
        return false;
    }
    if (!present) {
        return true;
    }
    if (!read_reference_pair(reader, state, 144U)) {
        return false;
    }
    bool notFlag0 = false;
    if (!read_raw(reader, 1, notFlag0)) {
        return false;
    }
    bool leadPresent = true;
    std::uint8_t flags = notFlag0 ? 0x04U : 0x01U;
    if (!notFlag0) {
        float autoaim = 0.0F;
        if (!read_real(reader, 4, 0.0F, 1.0F, autoaim) || !read_raw(reader, 1, leadPresent)) {
            return false;
        }
        store(state, 132U, autoaim);
        if (leadPresent) {
            flags |= 0x04U;
        }
    }
    store(state, 128U, flags);
    if (!leadPresent) {
        return true;
    }
    bool zeroVector = false;
    if (!read_raw(reader, 1, zeroVector)) {
        return false;
    }
    if (zeroVector) {
        return true;
    }
    std::uint16_t direction = 0;
    std::uint8_t magnitude = 0;
    return read_raw(reader, 15, direction) && read_raw(reader, 7, magnitude);
}

/** Adapts the stock payload reader to the indexed lane callback. */
[[nodiscard]] bool read_stock_payload(const void*,
                                      std::uint8_t,
                                      encoding::bits::Reader& reader,
                                      ControlState& output) noexcept {
    return read_control_state_payload(reader, output);
}

/** Decodes one present payload inside the caller's fixed packet ceiling. */
[[nodiscard]] bool read_payload(const ControlStatePayloadCodec& codec,
                                std::uint8_t index,
                                encoding::bits::Reader& reader,
                                ControlState& output) noexcept {
    if (codec.read == nullptr || codec.maximumPayloadBits > kMaximumControlStatePayloadBits) {
        return false;
    }
    const std::size_t before = reader.remaining_bits();
    return codec.read(codec.context, index, reader, output) && reader.remaining_bits() <= before
           && before - reader.remaining_bits() <= codec.maximumPayloadBits;
}

} // namespace

/** Decodes every conditional in the stock 160-byte control-state body. */
bool read_control_state_payload(encoding::bits::Reader& reader, ControlState& output) noexcept {
    encoding::bits::Reader candidateReader = reader;
    ControlState candidate{};
    std::uint32_t form = 0;
    if (!read_raw(candidateReader, 1, form)) {
        return false;
    }
    store(candidate, 0U, form);
    std::uint8_t subtype = 0xFFU;
    if (form == 1U && !read_raw(candidateReader, 4, subtype)) {
        return false;
    }
    store(candidate, 4U, subtype);

    float throttleX = 0.0F;
    float throttleY = 0.0F;
    float boost = 0.0F;
    if (!read_real(candidateReader, 7, -1.0F, 1.0F, throttleX)
        || !read_real(candidateReader, 7, -1.0F, 1.0F, throttleY)) {
        return false;
    }
    store(candidate, 8U, throttleX);
    store(candidate, 12U, throttleY);
    bool present = false;
    if (!read_raw(candidateReader, 1, present)
        || (present && !read_real(candidateReader, 5, 0.0F, 1.0F, boost))) {
        return false;
    }
    store(candidate, 16U, boost);

    float secondaryTrigger = 0.0F;
    if (!read_raw(candidateReader, 1, present)
        || (present && !read_real(candidateReader, 7, 0.0F, 1.0F, secondaryTrigger))) {
        return false;
    }
    store(candidate, 28U, secondaryTrigger);
    std::uint16_t field32 = 0;
    if (!read_raw(candidateReader, 1, present)
        || (present && !read_raw(candidateReader, 4, field32))) {
        return false;
    }
    store(candidate, 32U, field32);

    bool primaryBranch = false;
    if (!read_raw(candidateReader, 1, primaryBranch)) {
        return false;
    }
    store(candidate, 34U, primaryBranch);
    if (primaryBranch) {
        float primaryTrigger = 0.0F;
        if (!read_raw(candidateReader, 1, present)
            || (present && !read_real(candidateReader, 7, 0.0F, 1.0F, primaryTrigger))) {
            return false;
        }
        store(candidate, 24U, primaryTrigger);
        std::uint8_t rawMode = 0;
        bool noWeapon = false;
        if (!read_raw(candidateReader, 3, rawMode) || !read_raw(candidateReader, 1, noWeapon)) {
            return false;
        }
        store(candidate, 36U, static_cast<std::int32_t>(rawMode) - 1);
        std::int32_t weapon = -1;
        if (!noWeapon) {
            std::uint8_t rawWeapon = 0;
            if (!read_raw(candidateReader, 2, rawWeapon)) {
                return false;
            }
            weapon = rawWeapon;
        }
        store(candidate, 40U, weapon);
        candidate.bytes[44] |= std::byte{0x01};
        bool aimPresent = false;
        if (!read_primary_aim(candidateReader, candidate, aimPresent)) {
            return false;
        }
        store(candidate, 45U, aimPresent);
        store(candidate, 46U, aimPresent);
    } else {
        bool containerPresent = false;
        if (!read_raw(candidateReader, 1, containerPresent)) {
            return false;
        }
        store(candidate, 46U, containerPresent);
        if (containerPresent && !read_reference_pair(candidateReader, candidate, 144U)) {
            return false;
        }
    }

    reader = candidateReader;
    output = candidate;
    return true;
}

/** Returns the exact stock body decoder behind the generic indexed lane. */
ControlStatePayloadCodec control_state_payload_codec() noexcept {
    return {nullptr, read_stock_payload, kStockControlStatePayloadBits};
}

/** Reads each indexed replacement before committing the complete lane. */
bool read_control_state_lane(encoding::bits::Reader& reader,
                             const ControlStatePayloadCodec& codec,
                             ControlStateBatch& output) noexcept {
    encoding::bits::Reader candidateReader = reader;
    ControlStateBatch candidate{};
    while (true) {
        std::uint64_t rowPresent = 0;
        if (!candidateReader.read(kLaneFlagWidth, rowPresent)) {
            return false;
        }
        if (rowPresent == 0) {
            reader = candidateReader;
            output = candidate;
            return true;
        }
        if (candidate.count == candidate.rows.size()) {
            return false;
        }
        std::uint64_t index = 0;
        std::uint64_t payloadPresent = 0;
        if (!candidateReader.read(kControlIndexWidth, index)
            || !candidateReader.read(kLaneFlagWidth, payloadPresent)) {
            return false;
        }
        ControlStateRow& row = candidate.rows[candidate.count];
        row.index = static_cast<std::uint8_t>(index);
        row.payloadPresent = payloadPresent != 0;
        if (!row.payloadPresent || !read_payload(codec, row.index, candidateReader, row.state)) {
            return false;
        }
        ++candidate.count;
    }
}

/** Delegates the lane or accepts the exact empty-list form. */
bool read_lane1(encoding::bits::Reader& reader, const Lane1Codec& codec) noexcept {
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

} // namespace sunrise::middleware::gameplay::external
