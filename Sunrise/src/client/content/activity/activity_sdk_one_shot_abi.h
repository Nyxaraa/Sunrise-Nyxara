#pragma once

#include <cstddef>
#include <cstdint>

namespace sunrise::client::content::activity::sdk_generation::one_shot::abi {

// Progress and result records carry this ABI version.
inline constexpr std::uint32_t kVersion = 1;
// Digests are SHA-256, so 32 bytes.
inline constexpr std::size_t kDigestSize = 32;

struct Progress final {
    std::uint32_t abiVersion{kVersion};
    std::uint32_t status{};
    std::uint32_t current{};
    std::uint32_t total{};
    std::uint32_t scenarioTag{};
    const char* detail{};
    std::uint32_t detailLength{};
};

/** Fixed result block one offline build writes back across the ABI boundary. */
struct Result final {
    std::uint32_t abiVersion{kVersion};
    std::uint32_t status{};
    std::uint32_t scenarioCount{};
    std::uint32_t activityRootCount{};
    std::uint32_t activityCount{};
    std::uint32_t builtScenarioCount{};
    std::uint32_t reusedScenarioCount{};
    std::uint32_t luaFileCount{};
    std::uint64_t packBytes{};
    std::uint8_t payloadSha256[kDigestSize]{};
};

using CancelProbe = bool (*)(void* context) noexcept;
using ProgressSink = void (*)(void* context, const Progress* progress) noexcept;

using GenerateFunction = std::uint32_t (*)(const wchar_t* packageDirectory,
                                           const wchar_t* cacheArtifactDirectory,
                                           const wchar_t* outputArtifactDirectory,
                                           CancelProbe cancel,
                                           void* cancelContext,
                                           ProgressSink progress,
                                           void* progressContext,
                                           Result* output) noexcept;

using AcceptGeneratedFunction = std::uint32_t (*)(const wchar_t* artifactDirectory,
                                                  const std::uint8_t* sourceFingerprint,
                                                  std::uint32_t sourceFingerprintSize,
                                                  CancelProbe cancel,
                                                  void* cancelContext,
                                                  Result* output) noexcept;

using PublishGeneratedFunction = std::uint32_t (*)(const wchar_t* stageArtifactDirectory,
                                                   const wchar_t* outputArtifactDirectory,
                                                   const std::uint8_t* sourceFingerprint,
                                                   std::uint32_t sourceFingerprintSize,
                                                   CancelProbe cancel,
                                                   void* cancelContext,
                                                   Result* output) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::one_shot::abi
