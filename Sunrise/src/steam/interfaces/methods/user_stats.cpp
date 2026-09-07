#include <array>
#include <cstdio>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../state/steam/steam_state.h"
#include "../internal.h"

namespace sunrise::steam::interfaces::methods {
namespace {

namespace stats = sunrise::state::steam;

/** Steam callback id for receiving the current user stats. */
constexpr int kUserStatsReceivedCallback = 1101;
/** The user-stats callback is 24 bytes. */
constexpr std::size_t kUserStatsReceivedSize = 24;

/** Steam current-user-stats callback layout. */
struct UserStatsReceived {
    std::uint64_t gameId{};
    int result{};
    DWORD padding{};
    std::uint64_t userId{};
};

static_assert(sizeof(UserStatsReceived) == kUserStatsReceivedSize);

} // namespace

/** @return True when the stats callback is queued. */
bool request_current_stats([[maybe_unused]] void* self) noexcept {
    const UserStatsReceived received{app_id(), kResultOk, 0, local_steam_id()};
    return queue_callback(kUserStatsReceivedCallback, 0, &received, sizeof(received));
}

/**
 * Reports the retained unlock state for one resolved achievement name.
 * @param achieved Receives the retained state, and false for a name never unlocked.
 * @return True when the name and the output storage are both usable.
 */
bool get_achievement([[maybe_unused]] void* self, const char* name, bool* achieved) noexcept {
    if (achieved == nullptr) {
        return false;
    }
    const std::string_view resolved = text_view(name);
    *achieved = stats::achievement_unlocked(resolved);
    return !resolved.empty();
}

/**
 * Retains one achievement unlock. The Client resolves an installed name before calling.
 * @return True only once the unlock is held in State, because the Client never retries.
 */
bool set_achievement([[maybe_unused]] void* self, const char* name) noexcept {
    const bool stored = stats::unlock_achievement(text_view(name));
    core::log::write(core::log::Channel::client,
                     core::log::Level::debug,
                     stored ? "ev=steam stage=achievement_set result=ok"
                            : "ev=steam stage=achievement_set result=refuse");
    return stored;
}

/**
 * Confirms the retained achievement state. Each unlock is already held by set_achievement, so
 * there is nothing pending to flush here.
 * @return True, always. The Client clears its dirty byte either way and never retries.
 */
bool store_stats([[maybe_unused]] void* self) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=steam stage=achievement_store unlocked=%zu",
                                      stats::unlocked_achievement_count());
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return true;
}

} // namespace sunrise::steam::interfaces::methods
