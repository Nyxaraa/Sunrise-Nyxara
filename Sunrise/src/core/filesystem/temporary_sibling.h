#pragma once

namespace sunrise::core::path {

/**
 * Removes bounded, old writer-owned temporary siblings whose process has stopped.
 * A sibling is named final.process.thread.sequence.tmp, so a crashed writer leaves one behind.
 * @param finalPath Null-terminated final file path that owns the sibling names.
 */
void remove_stale_siblings(const wchar_t* finalPath) noexcept;

/**
 * Publishes one complete sibling while open delete-sharing readers keep the old file.
 * @param temporaryPath Existing writer-owned sibling path.
 * @param finalPath Final path to create or replace.
 * @return True when the sibling became the complete final file.
 */
[[nodiscard]] bool publish_sibling(const wchar_t* temporaryPath, const wchar_t* finalPath) noexcept;

} // namespace sunrise::core::path
