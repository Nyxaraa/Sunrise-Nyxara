#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::state::steam {

/** Steam publishes at most 20 rich-presence keys for one user. */
inline constexpr std::size_t kRichPresenceKeyCapacity = 20;
/** Steam caps a rich-presence key at 64 bytes, including the terminator. */
inline constexpr std::size_t kRichPresenceKeySize = 64;
/** Steam caps a rich-presence value at 256 bytes, including the terminator. */
inline constexpr std::size_t kRichPresenceValueSize = 256;
/** Steam caps an achievement API name at 128 bytes, including the terminator. */
inline constexpr std::size_t kAchievementNameSize = 128;
/** Fixed unlock table. Not a Steam limit; it keeps achievement state off the heap. */
inline constexpr std::size_t kAchievementCapacity = 256;
/** The Client's lobby chat producer hands over exactly 0x410 body bytes. */
inline constexpr std::size_t kChatBodySize = 0x410;
/** Fixed chat ring. The Client reads each entry on its callback, so a short history suffices. */
inline constexpr std::size_t kChatCapacity = 16;
/** Chat ids start at 1, so a cleared ring slot can never answer a lookup. */
inline constexpr std::uint32_t kFirstChatId = 1;
/** A zero lobby id is never a valid chat destination. */
inline constexpr std::uint64_t kAbsentLobby = 0;

/** One retained rich-presence pair for the local user. */
struct RichPresenceEntry {
    std::array<char, kRichPresenceKeySize> key{};
    std::array<char, kRichPresenceValueSize> value{};
};

/** One retained achievement unlock, keyed by its resolved API name. */
struct AchievementEntry {
    std::array<char, kAchievementNameSize> name{};
};

/** One retained lobby chat message, holding the Client's exact submitted bytes. */
struct ChatRecord {
    std::uint64_t lobby{kAbsentLobby};
    std::uint64_t sender{};
    std::uint32_t chatId{};
    std::uint32_t size{};
    std::array<std::byte, kChatBodySize> body{};
};

/** Process-global Steam shim state owned by the root State object. */
struct SteamState {
    std::array<RichPresenceEntry, kRichPresenceKeyCapacity> richPresence{};
    std::array<AchievementEntry, kAchievementCapacity> achievements{};
    std::array<ChatRecord, kChatCapacity> chat{};
    std::size_t richPresenceCount{};
    std::size_t achievementCount{};
    /** Ring position overwritten next. The ring never grows, so the oldest message falls out. */
    std::size_t chatWrite{};
    /** Next message identity handed to the Client, which passes it back to read the record. */
    std::uint32_t nextChatId{kFirstChatId};
};

} // namespace sunrise::state::steam
