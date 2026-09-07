#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sunrise::client::content::activity::sdk_generation::estate_validation {

using Digest = std::array<std::byte, 32>;
using CancelProbe = bool (*)(void* context) noexcept;

enum class Status : std::uint8_t {
    ready,
    cancelled,
    invalidInput,
    notGenerated = 6,
    publication,
};

/** Optional counts produced by the package pass and matched against its retained receipt. */
struct ExpectedCounts final {
    std::uint32_t scenarioCount{};
    std::uint32_t activityRootCount{};
    std::uint32_t activityCount{};
};

/** Generation identity and counts retained from one successful package pass. */
struct Result final {
    std::uint32_t scenarioCount{};
    std::uint32_t activityRootCount{};
    std::uint32_t activityCount{};
    std::uint32_t builtScenarioCount{};
    std::uint32_t reusedScenarioCount{};
    std::uint32_t luaFileCount{};
    Digest payloadSha256{};
    std::uint64_t packBytes{};
};

/** @return Stable diagnostic name for one retained-result lookup. */
[[nodiscard]] const char* status_name(Status value) noexcept;

/** Retains one successful generation result without reopening its output tree. */
[[nodiscard]] bool remember(std::wstring_view artifactDirectory,
                            const Digest& sourceFingerprint,
                            const Result& result) noexcept;

/** Retains a canonical owned path without allocating after publication. */
void remember_canonical(std::wstring artifactDirectory,
                        const Digest& sourceFingerprint,
                        const Result& result) noexcept;

/** Clears a retained result before another pass targets the same path. */
void forget(std::wstring_view artifactDirectory) noexcept;

/** Clears a retained result for one already-canonical path without allocating. */
void forget_canonical(std::wstring_view artifactDirectory) noexcept;

/** Returns only a process-local result retained by the deterministic generator. */
[[nodiscard]] Status accept_generated(std::wstring_view artifactDirectory,
                                      const Digest& sourceFingerprint,
                                      const ExpectedCounts* expected,
                                      CancelProbe cancel,
                                      void* cancelContext,
                                      Result& output) noexcept;

/** Publishes one retained sibling estate through one atomic tree transaction. */
[[nodiscard]] Status publish_generated(std::wstring_view stageArtifactDirectory,
                                       std::wstring_view outputArtifactDirectory,
                                       const Digest& sourceFingerprint,
                                       CancelProbe cancel,
                                       void* cancelContext,
                                       Result& output) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::estate_validation
