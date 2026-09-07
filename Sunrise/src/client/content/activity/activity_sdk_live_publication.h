#pragma once

#include <cstdint>
#include <string>

namespace sunrise::client::content::activity::sdk_generation::live_publication {

/** Isolated output paths owned by one live generation pass until commit. */
struct Stage final {
    std::wstring root{};
    std::wstring sdkDirectory{};
    std::wstring packPath{};
    std::wstring catalogPath{};
    std::wstring luaDirectory{};
};

enum class Status : std::uint8_t {
    ready,
    invalidInput,
    stageAllocation,
    backupCollision,
    backupFailure,
    commitFailure,
    finalizeFailure,
    rollbackFailure,
};

/** Rename hook used by production and deterministic failure-injection tests. */
using MoveOperation = bool (*)(void* context,
                               const wchar_t* source,
                               const wchar_t* target) noexcept;
/** Called only after catalog-last publication and before backups are discarded. */
using FinalizeOperation = bool (*)(void* context) noexcept;

/** @return Stable diagnostic name for one live publication result. */
[[nodiscard]] const char* status_name(Status value) noexcept;

/** Allocates one writer-owned isolated stage beside the live pack. */
[[nodiscard]] Status allocate(const wchar_t* finalPackPath, Stage& output) noexcept;

/**
 * Replaces only the owned Lua subtree, pack file, and catalog file, in that order. The catalog
 * rename is the commit point. Any failure before finalization restores all previous owned paths.
 */
[[nodiscard]] Status publish(const Stage& stage,
                             const wchar_t* finalSdkDirectory,
                             const wchar_t* finalPackPath,
                             const wchar_t* finalCatalogPath,
                             MoveOperation move,
                             void* moveContext,
                             FinalizeOperation finalize,
                             void* finalizeContext) noexcept;

/** Removes one caller-owned stage without following reparse points. */
[[nodiscard]] bool discard(const Stage& stage) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::live_publication
