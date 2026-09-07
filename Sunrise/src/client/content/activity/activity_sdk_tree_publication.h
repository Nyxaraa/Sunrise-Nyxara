#pragma once

#include <cstdint>

namespace sunrise::client::content::activity::sdk_generation::tree_publication {

enum class Status : std::uint8_t {
    ready,
    invalidInput,
    backupCollision,
    backupFailure,
    commitFailure,
    rollbackFailure,
};

/** Rename hook used by the production path and deterministic failure tests. */
using MoveOperation = bool (*)(void* context,
                               const wchar_t* source,
                               const wchar_t* target) noexcept;

/** @return Stable diagnostic name for one tree publication outcome. */
[[nodiscard]] const char* status_name(Status value) noexcept;

/** Replaces one complete artifact tree and restores the old tree if the commit rename fails. */
[[nodiscard]] Status publish(const wchar_t* stage,
                             const wchar_t* output,
                             MoveOperation move,
                             void* moveContext) noexcept;

/** Removes one caller-owned failed stage without following reparse points. */
[[nodiscard]] bool discard(const wchar_t* stage) noexcept;

} // namespace sunrise::client::content::activity::sdk_generation::tree_publication
