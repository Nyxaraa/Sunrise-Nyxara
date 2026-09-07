#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "definition.h"

namespace sunrise::state::steam {

/**
 * Retains one rich-presence pair for the local user, replacing a pair with the same key.
 * An empty value removes the key, which is the Steam contract for that call.
 * @return True when the pair is retained, or its key was removed.
 */
[[nodiscard]] bool set_rich_presence(std::string_view key, std::string_view value) noexcept;

/** Drops every retained rich-presence pair for the local user. */
void clear_rich_presence() noexcept;

/**
 * @param key Exact key to look up.
 * @return Retained value, or an empty string. It stays valid until the table changes.
 */
[[nodiscard]] const char* rich_presence_value(std::string_view key) noexcept;

/** @return Retained rich-presence pair count for the local user. */
[[nodiscard]] int rich_presence_key_count() noexcept;

/**
 * @param index Zero-based position in the retained table.
 * @return Retained key, or an empty string. It stays valid until the table changes.
 */
[[nodiscard]] const char* rich_presence_key(int index) noexcept;

/**
 * Retains one achievement unlock keyed by its resolved API name.
 * @return True when the name is already unlocked, or the fixed table had room for it.
 */
[[nodiscard]] bool unlock_achievement(std::string_view name) noexcept;

/** @return True when the resolved name is retained as unlocked. */
[[nodiscard]] bool achievement_unlocked(std::string_view name) noexcept;

/** @return Count of retained unlocks. */
[[nodiscard]] std::size_t unlocked_achievement_count() noexcept;

/**
 * Retains one lobby chat message in the fixed ring.
 * @param lobby Destination lobby the Client named.
 * @param sender Member the message is attributed to.
 * @param body Exact Client body, at most kChatBodySize bytes.
 * @param chatId Receives the identity the Client passes back to read the record.
 * @return True when the body fits and the record was retained.
 */
[[nodiscard]] bool retain_chat_message(std::uint64_t lobby,
                                       std::uint64_t sender,
                                       std::span<const std::byte> body,
                                       std::uint32_t& chatId) noexcept;

/**
 * Copies one retained chat message out of the ring.
 * @param output Cleared, then filled only when the lobby and id both match a live record.
 * @return True when the ring still holds that message.
 */
[[nodiscard]] bool
chat_message(std::uint64_t lobby, std::uint32_t chatId, ChatRecord& output) noexcept;

} // namespace sunrise::state::steam
