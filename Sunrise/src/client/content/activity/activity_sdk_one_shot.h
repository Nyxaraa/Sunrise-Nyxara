#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "activity_sdk_generation_worker.h"

namespace sunrise::client::content::activity::sdk_generation::one_shot {

enum class Status : std::uint8_t {
    ready,
    cancelled,
    busy,
    invalidInput,
    stageAllocation,
    generation,
    notGenerated = 6,
    publication,
};

/** In-process paths and a borrowed reader source for one isolated generation. */
struct Request final {
    state::activity_sdk::generated_world::Digest sourceFingerprint{};
    const middleware::content::packages::reader::BlockKeys* keys{};
    std::wstring_view packageDirectory{};
    /** Reserved for ABI compatibility. Generation never reopens a prior estate. */
    std::wstring_view cacheArtifactDirectory{};
    std::wstring_view outputArtifactDirectory{};
};

/** Final identity and counts returned only after the complete tree replaces output. */
struct Result final {
    OfflineBuildResult build{};
    std::array<std::byte, 32> payloadSha256{};
    std::uint64_t packBytes{};
    std::uint32_t luaFiles{};
};

/** @return Stable diagnostic name for one one-shot outcome. */
[[nodiscard]] const char* status_name(Status value) noexcept;

/** Internal in-process/test boundary that builds into one isolated dedicated SDK-only estate. */
[[nodiscard]] Status run(const Request& request,
                         OfflineCancelProbe cancel,
                         void* cancelContext,
                         OfflineProgressSink progress,
                         void* progressContext,
                         Result& output) noexcept;

/** Production in-DLL wrapper that borrows process-local package keys and zeroes its sole copy. */
[[nodiscard]] Status run_process_local(std::wstring_view packageDirectory,
                                       std::wstring_view cacheArtifactDirectory,
                                       std::wstring_view outputArtifactDirectory,
                                       OfflineCancelProbe cancel,
                                       void* cancelContext,
                                       OfflineProgressSink progress,
                                       void* progressContext,
                                       Result& output) noexcept;

/** Returns the retained result for one generation completed by this process. */
[[nodiscard]] Status
accept_generated(std::wstring_view artifactDirectory,
                 const state::activity_sdk::generated_world::Digest& sourceFingerprint,
                 OfflineCancelProbe cancel,
                 void* cancelContext,
                 Result& output) noexcept;

/** Replaces one sibling SDK-only estate without reopening generated files. */
[[nodiscard]] Status
publish_generated(std::wstring_view stageArtifactDirectory,
                  std::wstring_view outputArtifactDirectory,
                  const state::activity_sdk::generated_world::Digest& sourceFingerprint,
                  OfflineCancelProbe cancel,
                  void* cancelContext,
                  Result& output) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::one_shot
