#include "sobject_payload_codec.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace sunrise::middleware::gameplay::external {
namespace {

/** Fixed fields use the widths the native type-0 codecs carry. */
constexpr std::uint8_t kRsatSelectorWidth = 6;
constexpr std::uint8_t kRsatTagWidth = 32;
constexpr std::uint8_t kAngleCodeWidth = 7;
constexpr std::uint8_t kRawFloatWidth = 32;
constexpr std::uint8_t kTailChunkWidth = 64;
constexpr std::uint8_t kMaximumAngleCode = 0x7F;
/** A transform names X, Y and Z. The fourth homogeneous lane is implied. */
constexpr std::size_t kPositionComponents = 3;

static_assert(std::is_trivially_copyable_v<SobjectBaseline>);
static_assert(std::is_trivially_copyable_v<SobjectUpdate>);
static_assert(sizeof(SobjectBaseline) <= kTypePayloadStateCapacity);
static_assert(sizeof(SobjectUpdate) <= kTypePayloadStateCapacity);

/** Copies one typed state into the callback-owned fixed buffer. */
template <typename Value>
[[nodiscard]] bool store_state(const Value& value, TypePayload& output) noexcept {
    static_assert(std::is_trivially_copyable_v<Value>);
    static_assert(sizeof(Value) <= kTypePayloadStateCapacity);
    TypePayload candidate{};
    std::memcpy(candidate.state.data(), &value, sizeof(value));
    candidate.byteCount = static_cast<std::uint16_t>(sizeof(value));
    output = candidate;
    return true;
}

/** Copies one exact typed layout out of callback-owned state. */
template <typename Value>
[[nodiscard]] bool load_state(const TypePayload& payload, Value& output) noexcept {
    static_assert(std::is_trivially_copyable_v<Value>);
    if (payload.byteCount != sizeof(Value)) {
        return false;
    }
    Value candidate{};
    std::memcpy(&candidate, payload.state.data(), sizeof(candidate));
    output = candidate;
    return true;
}

/** Resolves one tag and rejects a stale, unproved, or over-wide shape. */
[[nodiscard]] bool resolve_shape(const SobjectPayloadCodecContext& context,
                                 std::uint32_t rsatTag,
                                 SobjectRsatShape& output) noexcept {
    if (rsatTag == 0 || context.resolveShape == nullptr) {
        return false;
    }
    SobjectRsatShape candidate{};
    if (!context.resolveShape(context.shapeContext, rsatTag, candidate)
        || candidate.rsatTag != rsatTag || !candidate.evidenceValidated
        || candidate.dynamicPresenceBitCount > kMaximumSobjectDynamicPresenceBits) {
        return false;
    }
    output = candidate;
    return true;
}

/** @return True for the two compression modes with an exact wire arm. */
[[nodiscard]] bool valid_compression(SobjectPositionCompression compression) noexcept {
    return compression == SobjectPositionCompression::disabled
           || compression == SobjectPositionCompression::enabledRaw;
}

/** @return True when the minimum transform can be represented without invention. */
[[nodiscard]] bool valid_update(const SobjectPayloadCodecContext& context,
                                const SobjectUpdate& update) noexcept {
    return update.transformPresent && update.rotation == SobjectRotationEncoding::zAxisShortcut
           && update.angleCode <= kMaximumAngleCode
           && valid_compression(context.positionCompression)
           && update.positionCompression == context.positionCompression
           && std::isfinite(update.position[0]) && std::isfinite(update.position[1])
           && std::isfinite(update.position[2]) && update.position[kPositionComponents] == 1.0F
           && !update.parentPresent && !update.streamSourcePresent;
}

/** Writes raw XYZ bits in component order. */
[[nodiscard]] bool write_position(encoding::bits::Writer& writer,
                                  const SobjectUpdate& update) noexcept {
    for (std::size_t index = 0; index < kPositionComponents; ++index) {
        if (!writer.write(std::bit_cast<std::uint32_t>(update.position[index]), kRawFloatWidth)) {
            return false;
        }
    }
    return true;
}

/** Reads raw XYZ bits and restores the implied homogeneous lane. */
[[nodiscard]] bool read_position(encoding::bits::Reader& reader, SobjectUpdate& output) noexcept {
    for (std::size_t index = 0; index < kPositionComponents; ++index) {
        std::uint64_t bits = 0;
        if (!reader.read(kRawFloatWidth, bits)) {
            return false;
        }
        output.position[index] = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
    }
    output.position[kPositionComponents] = 1.0F;
    return std::isfinite(output.position[0]) && std::isfinite(output.position[1])
           && std::isfinite(output.position[2]);
}

/** Writes an all-zero RSAT component-presence tail. */
[[nodiscard]] bool write_dynamic_tail(encoding::bits::Writer& writer,
                                      std::size_t bitCount) noexcept {
    while (bitCount != 0) {
        const std::uint8_t width = static_cast<std::uint8_t>(
            (std::min)(bitCount, static_cast<std::size_t>(kTailChunkWidth)));
        if (!writer.write(0, width)) {
            return false;
        }
        bitCount -= width;
    }
    return true;
}

/** Reads the RSAT tail and rejects every nonzero component presence. */
[[nodiscard]] bool read_dynamic_tail(encoding::bits::Reader& reader,
                                     std::size_t bitCount) noexcept {
    while (bitCount != 0) {
        const std::uint8_t width = static_cast<std::uint8_t>(
            (std::min)(bitCount, static_cast<std::size_t>(kTailChunkWidth)));
        std::uint64_t value = 0;
        if (!reader.read(width, value) || value != 0) {
            return false;
        }
        bitCount -= width;
    }
    return true;
}

/** Reads the exact fixed baseline and validates its RSAT before publication. */
[[nodiscard]] bool read_baseline(const SobjectPayloadCodecContext& context,
                                 encoding::bits::Reader& reader,
                                 TypePayload& output) noexcept {
    std::uint64_t selector = 0;
    bool present = false;
    std::uint64_t rsatTag = 0;
    bool placementIdentity = false;
    if (!reader.read(kRsatSelectorWidth, selector) || selector != kSobjectRsatSelector
        || !read_flag(reader, present) || !present || !reader.read(kRsatTagWidth, rsatTag)
        || !read_flag(reader, placementIdentity) || !placementIdentity) {
        return false;
    }
    SobjectRsatShape shape{};
    if (!resolve_shape(context, static_cast<std::uint32_t>(rsatTag), shape)) {
        return false;
    }
    return store_sobject_baseline({static_cast<std::uint32_t>(rsatTag), placementIdentity}, output);
}

/** Writes the exact fixed baseline after validating its RSAT and identity flag. */
[[nodiscard]] bool write_baseline(const SobjectPayloadCodecContext& context,
                                  const TypePayload& payload,
                                  encoding::bits::Writer& writer) noexcept {
    SobjectBaseline baseline{};
    SobjectRsatShape shape{};
    return load_sobject_baseline(payload, baseline) && baseline.placementIdentity
           && resolve_shape(context, baseline.rsatTag, shape)
           && writer.write(kSobjectRsatSelector, kRsatSelectorWidth) && write_flag(writer, true)
           && writer.write(baseline.rsatTag, kRsatTagWidth) && write_flag(writer, true);
}

/** Reads the supported transform and the exact zero dynamic tail. */
[[nodiscard]] bool read_update(const SobjectPayloadCodecContext& context,
                               const TypePayload& baselinePayload,
                               encoding::bits::Reader& reader,
                               TypePayload& output) noexcept {
    SobjectBaseline baseline{};
    SobjectRsatShape shape{};
    SobjectUpdate candidate{};
    bool shortcut = false;
    std::uint64_t angleCode = 0;
    bool homogeneous = false;
    if (!load_sobject_baseline(baselinePayload, baseline) || !baseline.placementIdentity
        || !resolve_shape(context, baseline.rsatTag, shape)
        || !read_flag(reader, candidate.transformPresent) || !candidate.transformPresent
        || !read_flag(reader, shortcut) || !shortcut || !read_flag(reader, candidate.negativeZ)
        || !reader.read(kAngleCodeWidth, angleCode) || !read_flag(reader, homogeneous)
        || !homogeneous) {
        return false;
    }
    candidate.angleCode = static_cast<std::uint8_t>(angleCode);
    candidate.rotation = SobjectRotationEncoding::zAxisShortcut;
    candidate.positionCompression = context.positionCompression;
    if (!valid_compression(candidate.positionCompression)) {
        return false;
    }
    if (candidate.positionCompression == SobjectPositionCompression::enabledRaw) {
        bool compressedSelector = false;
        if (!read_flag(reader, compressedSelector) || compressedSelector) {
            return false;
        }
    }
    if (!read_position(reader, candidate) || !read_flag(reader, candidate.parentPresent)
        || candidate.parentPresent || !read_flag(reader, candidate.streamSourcePresent)
        || candidate.streamSourcePresent
        || !read_dynamic_tail(reader, shape.dynamicPresenceBitCount)
        || !valid_update(context, candidate)) {
        return false;
    }
    return store_sobject_update(candidate, output);
}

/** Writes the supported transform and an exact zero dynamic tail. */
[[nodiscard]] bool write_update(const SobjectPayloadCodecContext& context,
                                const TypePayload& baselinePayload,
                                const TypePayload& updatePayload,
                                encoding::bits::Writer& writer) noexcept {
    SobjectBaseline baseline{};
    SobjectRsatShape shape{};
    SobjectUpdate update{};
    if (!load_sobject_baseline(baselinePayload, baseline) || !baseline.placementIdentity
        || !resolve_shape(context, baseline.rsatTag, shape)
        || !load_sobject_update(updatePayload, update) || !valid_update(context, update)
        || !write_flag(writer, true) || !write_flag(writer, true)
        || !write_flag(writer, update.negativeZ) || !writer.write(update.angleCode, kAngleCodeWidth)
        || !write_flag(writer, true)) {
        return false;
    }
    if (update.positionCompression == SobjectPositionCompression::enabledRaw
        && !write_flag(writer, false)) {
        return false;
    }
    return write_position(writer, update) && write_flag(writer, false) && write_flag(writer, false)
           && write_dynamic_tail(writer, shape.dynamicPresenceBitCount);
}

/** Dispatches the two supported type-0 payload parts. */
[[nodiscard]] bool read_payload(const void* opaqueContext,
                                const EntityToken&,
                                EntityType type,
                                TypePayloadPart part,
                                const TypePayload* baseline,
                                encoding::bits::Reader& reader,
                                TypePayload& output) noexcept {
    if (opaqueContext == nullptr || type != EntityType::sobject) {
        return false;
    }
    const auto& context = *static_cast<const SobjectPayloadCodecContext*>(opaqueContext);
    if (part == TypePayloadPart::baseline) {
        return baseline == nullptr && read_baseline(context, reader, output);
    }
    return baseline != nullptr && read_update(context, *baseline, reader, output);
}

/** Dispatches the two supported type-0 payload parts. */
[[nodiscard]] bool write_payload(const void* opaqueContext,
                                 const EntityToken&,
                                 EntityType type,
                                 TypePayloadPart part,
                                 const TypePayload* baseline,
                                 const TypePayload& payload,
                                 encoding::bits::Writer& writer) noexcept {
    if (opaqueContext == nullptr || type != EntityType::sobject) {
        return false;
    }
    const auto& context = *static_cast<const SobjectPayloadCodecContext*>(opaqueContext);
    if (part == TypePayloadPart::baseline) {
        return baseline == nullptr && write_baseline(context, payload, writer);
    }
    return baseline != nullptr && write_update(context, *baseline, payload, writer);
}

} // namespace

bool store_sobject_baseline(const SobjectBaseline& baseline, TypePayload& output) noexcept {
    return store_state(baseline, output);
}

bool load_sobject_baseline(const TypePayload& payload, SobjectBaseline& output) noexcept {
    return load_state(payload, output);
}

bool store_sobject_update(const SobjectUpdate& update, TypePayload& output) noexcept {
    return store_state(update, output);
}

bool load_sobject_update(const TypePayload& payload, SobjectUpdate& output) noexcept {
    return load_state(payload, output);
}

TypePayloadCodec make_sobject_payload_codec(const SobjectPayloadCodecContext& context) noexcept {
    TypePayloadCodec codec{};
    codec.context = &context;
    codec.read = read_payload;
    codec.write = write_payload;
    codec.maximumBaselineBits = kSobjectBaselineBits;
    codec.maximumUpdateBits = kMaximumTypePayloadBits;
    return codec;
}

} // namespace sunrise::middleware::gameplay::external
