#include "steam_state.h"

#include <Windows.h>

#include <array>
#include <cstring>
#include <limits>

#include "../runtime/storage/internal.h"

namespace sunrise::state::steam {
namespace {

/** Answered for every absent key, index and value, so no caller ever receives a null string. */
constexpr char kEmptyText[] = "";

/**
 * Copies text into fixed storage.
 * @param output Cleared before the copy, so the result is always terminated.
 * @return False when the text plus its terminator does not fit.
 */
template <std::size_t Size>
[[nodiscard]] bool assign(std::array<char, Size>& output, std::string_view value) noexcept {
    if (value.size() >= Size) {
        return false;
    }
    output = {};
    if (!value.empty()) {
        std::memcpy(output.data(), value.data(), value.size());
    }
    return true;
}

/** @return Terminated fixed storage as a view. */
template <std::size_t Size>
[[nodiscard]] std::string_view text_of(const std::array<char, Size>& storage) noexcept {
    return std::string_view{storage.data()};
}

/**
 * Finds a rich-presence key. Runs under the State lock.
 * @return Index of the pair, or the retained count when the key is absent.
 */
[[nodiscard]] std::size_t find_rich_presence(const SteamState& state,
                                             std::string_view key) noexcept {
    for (std::size_t index = 0; index < state.richPresenceCount; ++index) {
        if (text_of(state.richPresence[index].key) == key) {
            return index;
        }
    }
    return state.richPresenceCount;
}

/**
 * Finds an achievement by its resolved API name. Runs under the State lock.
 * @return Index of the unlock, or the retained count when the name is absent.
 */
[[nodiscard]] std::size_t find_achievement(const SteamState& state,
                                           std::string_view name) noexcept {
    for (std::size_t index = 0; index < state.achievementCount; ++index) {
        if (text_of(state.achievements[index].name) == name) {
            return index;
        }
    }
    return state.achievementCount;
}

/** Removes one rich-presence pair and closes the gap. Runs under the State lock. */
void erase_rich_presence(SteamState& state, std::size_t index) noexcept {
    for (std::size_t next = index + 1; next < state.richPresenceCount; ++next) {
        state.richPresence[next - 1] = state.richPresence[next];
    }
    --state.richPresenceCount;
    state.richPresence[state.richPresenceCount] = {};
}

} // namespace

/** Retains, replaces or removes one rich-presence pair for the local user. */
bool set_rich_presence(std::string_view key, std::string_view value) noexcept {
    if (key.empty() || key.size() >= kRichPresenceKeySize
        || value.size() >= kRichPresenceValueSize) {
        return false;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    SteamState& state = runtime::storage::g_state.steam;
    const std::size_t index = find_rich_presence(state, key);
    const bool present = index < state.richPresenceCount;
    // Steam treats an empty value as a request to drop the key.
    if (value.empty()) {
        if (present) {
            erase_rich_presence(state, index);
        }
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return true;
    }
    if (!present && state.richPresenceCount == kRichPresenceKeyCapacity) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    // Both sizes were bounded on entry, so neither copy can be refused.
    RichPresenceEntry& entry = state.richPresence[index];
    (void)assign(entry.key, key);
    (void)assign(entry.value, value);
    if (!present) {
        ++state.richPresenceCount;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return true;
}

/** Drops every retained rich-presence pair. */
void clear_rich_presence() noexcept {
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    SteamState& state = runtime::storage::g_state.steam;
    state.richPresence = {};
    state.richPresenceCount = 0;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
}

/** Serves one retained rich-presence value back to the local user. */
const char* rich_presence_value(std::string_view key) noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const SteamState& state = runtime::storage::g_state.steam;
    const std::size_t index = find_rich_presence(state, key);
    const char* result =
        index < state.richPresenceCount ? state.richPresence[index].value.data() : kEmptyText;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return result;
}

/** Reports how many rich-presence pairs the local user currently publishes. */
int rich_presence_key_count() noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const auto count = static_cast<int>(runtime::storage::g_state.steam.richPresenceCount);
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return count;
}

/** Serves one retained rich-presence key by its table position. */
const char* rich_presence_key(int index) noexcept {
    if (index < 0) {
        return kEmptyText;
    }
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const SteamState& state = runtime::storage::g_state.steam;
    const auto slot = static_cast<std::size_t>(index);
    const char* result =
        slot < state.richPresenceCount ? state.richPresence[slot].key.data() : kEmptyText;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return result;
}

/** Retains one achievement unlock keyed by its resolved API name. */
bool unlock_achievement(std::string_view name) noexcept {
    if (name.empty() || name.size() >= kAchievementNameSize) {
        return false;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    SteamState& state = runtime::storage::g_state.steam;
    bool stored = find_achievement(state, name) < state.achievementCount;
    if (!stored && state.achievementCount < kAchievementCapacity) {
        // The name was bounded on entry, so the copy cannot be refused.
        (void)assign(state.achievements[state.achievementCount].name, name);
        ++state.achievementCount;
        stored = true;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return stored;
}

/** Reports the retained unlock state for one resolved achievement name. */
bool achievement_unlocked(std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const SteamState& state = runtime::storage::g_state.steam;
    const bool unlocked = find_achievement(state, name) < state.achievementCount;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return unlocked;
}

/** Reports how many achievements are retained as unlocked. */
std::size_t unlocked_achievement_count() noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const std::size_t count = runtime::storage::g_state.steam.achievementCount;
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return count;
}

/** Retains one lobby chat message and hands back the identity that reads it again. */
bool retain_chat_message(std::uint64_t lobby,
                         std::uint64_t sender,
                         std::span<const std::byte> body,
                         std::uint32_t& chatId) noexcept {
    chatId = 0;
    if (lobby == kAbsentLobby || body.empty() || body.size() > kChatBodySize) {
        return false;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    SteamState& state = runtime::storage::g_state.steam;
    ChatRecord& record = state.chat[state.chatWrite];
    record = {};
    record.lobby = lobby;
    record.sender = sender;
    record.chatId = state.nextChatId;
    record.size = static_cast<std::uint32_t>(body.size());
    std::memcpy(record.body.data(), body.data(), body.size());
    state.chatWrite = (state.chatWrite + 1) % kChatCapacity;
    // Ids stay nonzero across a wrap, so a cleared slot never matches a live lookup.
    state.nextChatId = state.nextChatId == (std::numeric_limits<std::uint32_t>::max)()
                           ? kFirstChatId
                           : state.nextChatId + 1;
    chatId = record.chatId;
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return true;
}

/** Copies one retained chat message back out of the ring. */
bool chat_message(std::uint64_t lobby, std::uint32_t chatId, ChatRecord& output) noexcept {
    output = {};
    if (lobby == kAbsentLobby || chatId == 0) {
        return false;
    }
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const SteamState& state = runtime::storage::g_state.steam;
    bool found = false;
    for (const ChatRecord& record : state.chat) {
        if (record.lobby == lobby && record.chatId == chatId) {
            output = record;
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return found;
}

} // namespace sunrise::state::steam
