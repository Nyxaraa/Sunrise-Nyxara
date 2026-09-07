#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "mission_script_world_sdk.h"

namespace sunrise::server::activity::mission::lua_vm {

/** Authenticated generated-world manifest families exposed to Mission Lua. */
enum class ManifestCollectionKind : std::uint8_t {
    scenarios,
    activityRoots,
    activityVariants,
    bindingCompleteness,
};

/** Closed value shapes returned by one generated-world manifest field read. */
enum class ManifestFieldKind : std::uint8_t {
    absent,
    unsignedInteger,
    unsignedDecimalString,
    boolean,
    string,
    bytes,
};

/** One copied or immediately consumed immutable manifest field value. */
struct ManifestFieldDefinition final {
    ManifestFieldKind kind{ManifestFieldKind::absent};
    std::uint64_t unsignedValue{};
    std::string_view stringValue{};
    std::array<std::byte, 32> bytesValue{};
    std::uint8_t valueCount{};
};

/** Which activity-variant-owned package-tag range is selected. */
enum class ManifestVariantTagKind : std::uint8_t {
    activityRootCandidates,
    scenarioNameCandidates,
    evidenceRoots,
};

/** One exact package locator copied from manifest classification evidence. */
struct ManifestLocatorDefinition final {
    std::uint32_t tag{};
    std::uint64_t offset{};
    std::uint32_t localRow{};
};

using ValidateManifestGeneration = bool (*)(const void*, const WorldGenerationIdentity&) noexcept;
using ManifestDefinitionCount = std::size_t (*)(const void*,
                                                const WorldGenerationIdentity&,
                                                ManifestCollectionKind) noexcept;
using ResolveManifestField = bool (*)(const void*,
                                      const WorldGenerationIdentity&,
                                      ManifestCollectionKind,
                                      std::uint32_t,
                                      std::string_view,
                                      ManifestFieldDefinition&) noexcept;
using ManifestVariantTagCount = std::size_t (*)(const void*,
                                                const WorldGenerationIdentity&,
                                                std::uint32_t,
                                                ManifestVariantTagKind) noexcept;
using ResolveManifestVariantTag = bool (*)(const void*,
                                           const WorldGenerationIdentity&,
                                           std::uint32_t,
                                           ManifestVariantTagKind,
                                           std::uint32_t,
                                           std::uint32_t&) noexcept;
using ManifestVariantLocatorCount = std::size_t (*)(const void*,
                                                    const WorldGenerationIdentity&,
                                                    std::uint32_t) noexcept;
using ResolveManifestVariantLocator = bool (*)(const void*,
                                               const WorldGenerationIdentity&,
                                               std::uint32_t,
                                               std::uint32_t,
                                               ManifestLocatorDefinition&) noexcept;

/** Immutable accessors whose context remains the caller-owned generated-world view. */
struct ManifestDefinitionApi final {
    const void* context{};
    WorldGenerationIdentity generation{};
    ValidateManifestGeneration validate{};
    ManifestDefinitionCount count{};
    ResolveManifestField resolveField{};
    ManifestVariantTagCount variantTagCount{};
    ResolveManifestVariantTag resolveVariantTag{};
    ManifestVariantLocatorCount variantLocatorCount{};
    ResolveManifestVariantLocator resolveVariantLocator{};
};

} // namespace sunrise::server::activity::mission::lua_vm
