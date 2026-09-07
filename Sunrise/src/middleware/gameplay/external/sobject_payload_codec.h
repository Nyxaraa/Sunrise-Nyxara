#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "external_entity_codec.h"

namespace sunrise::middleware::gameplay::external {

/** Type-0 create selector for an installed sobject RSAT tag. */
inline constexpr std::uint8_t kSobjectRsatSelector = 22;
/** The fixed type-0 create body consumes 40 bits. */
inline constexpr std::size_t kSobjectBaselineBits = 40;
/** The disabled-compression minimum update precedes its RSAT tail with 109 bits. */
inline constexpr std::size_t kSobjectDisabledCompressionUpdateBits = 109;
/** The enabled raw-compression minimum update adds one zero selector bit. */
inline constexpr std::size_t kSobjectEnabledRawUpdateBits = 110;
/** The tail must leave room for the largest supported fixed update prefix. */
inline constexpr std::size_t kMaximumSobjectDynamicPresenceBits =
    kMaximumTypePayloadBits - kSobjectEnabledRawUpdateBits;

/** The minimum codec accepts only the native Z-axis shortcut arm. */
enum class SobjectRotationEncoding : std::uint8_t {
    zAxisShortcut,
    unsupported,
};

/** Selects the package-defined position codec before a payload is parsed. */
enum class SobjectPositionCompression : std::uint8_t {
    disabled,
    enabledRaw,
    unsupported,
};

/** One package-derived RSAT shape approved for wire use. */
struct SobjectRsatShape {
    std::uint32_t rsatTag{};
    std::uint16_t dynamicPresenceBitCount{};
    bool evidenceValidated{};
};

/** Resolves one exact RSAT tag to its package-derived dynamic presence width. */
using ResolveSobjectRsatShape = bool (*)(const void* context,
                                         std::uint32_t rsatTag,
                                         SobjectRsatShape& output) noexcept;

/** Immutable dependencies for one type-0 payload codec. */
struct SobjectPayloadCodecContext {
    const void* shapeContext{};
    ResolveSobjectRsatShape resolveShape{};
    SobjectPositionCompression positionCompression{SobjectPositionCompression::unsupported};
};

/** Semantic state for the fixed 40-bit type-0 create body. */
struct SobjectBaseline {
    std::uint32_t rsatTag{};
    bool placementIdentity{true};
};

/** Semantic state for the supported transform-only type-0 update. */
struct SobjectUpdate {
    std::array<float, 4> position{0.0F, 0.0F, 0.0F, 1.0F};
    std::uint8_t angleCode{};
    SobjectRotationEncoding rotation{SobjectRotationEncoding::zAxisShortcut};
    SobjectPositionCompression positionCompression{SobjectPositionCompression::disabled};
    bool negativeZ{};
    bool transformPresent{true};
    bool parentPresent{};
    bool streamSourcePresent{};
};

/** Stores typed baseline state for the generic entity envelope. */
[[nodiscard]] bool store_sobject_baseline(const SobjectBaseline& baseline,
                                          TypePayload& output) noexcept;

/** Loads typed baseline state and rejects another callback's layout. */
[[nodiscard]] bool load_sobject_baseline(const TypePayload& payload,
                                         SobjectBaseline& output) noexcept;

/** Stores typed update state for the generic entity envelope. */
[[nodiscard]] bool store_sobject_update(const SobjectUpdate& update, TypePayload& output) noexcept;

/** Loads typed update state and rejects another callback's layout. */
[[nodiscard]] bool load_sobject_update(const TypePayload& payload, SobjectUpdate& output) noexcept;

/**
 * Builds a create-plus-update type-0 codec over an immutable caller-owned context.
 * Update-only records are refused by design: the update tail is sized from the same-record
 * create baseline, and the codec context carries no registry to look one up.
 */
[[nodiscard]] TypePayloadCodec
make_sobject_payload_codec(const SobjectPayloadCodecContext& context) noexcept;

} // namespace sunrise::middleware::gameplay::external
