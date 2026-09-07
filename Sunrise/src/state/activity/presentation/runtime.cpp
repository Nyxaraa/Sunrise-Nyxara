#include "runtime.h"
#include "movies.h"
#include "config_rules.h"
#include <array>
#include <mutex>
namespace sunrise::state::activity::presentation {
namespace {
struct Registration {
    Owner owner{};
    Config config{};
    std::uint16_t activity{0xFFFF};
    bool loadingMask{}, launchConsumed{};
};
MovieRequest movie;
MovieStatus status{MovieStatus::absent};
std::array<Registration, 16> registrations{};
std::mutex mutex;
}
bool publish(Owner owner, const Config& config, std::uint16_t activity) noexcept {
    if (!owner.session || !owner.generation || !valid_config(config)
        || (config.movieCount && activity == 0xFFFF)) return false;
    std::lock_guard guard(mutex);
    for (auto& row : registrations) {
        if (row.owner == owner) { row.config = config; row.activity = activity; return true; }
    }
    for (auto& row : registrations) {
        if (!row.owner.session) { row = {owner, config, activity}; return true; }
    }
    return false;
}
void remove(Owner owner) noexcept {
    std::lock_guard guard(mutex);
    for (auto& row : registrations) if (row.owner == owner) row = {};
}
void observe_launch(Owner owner, bool requested, bool arrived) noexcept {
    std::lock_guard guard(mutex);
    for (auto& row : registrations) {
        if (row.owner != owner || !row.owner.session) continue;
        row.launchConsumed |= arrived;
        row.loadingMask = requested && (row.config.suppressLoadingCinematics || row.config.maskLoadingScreen) && !row.launchConsumed;
    }
}
bool loading_mask_request(Owner& owner, std::uint16_t& activity, bool* loadingScreenOnly) noexcept {
    std::lock_guard guard(mutex);
    owner = {};
    activity = 0xFFFF;
    if (loadingScreenOnly) *loadingScreenOnly = false;
    for (const auto& row : registrations) {
        if (!row.loadingMask || !(row.config.suppressLoadingCinematics || row.config.maskLoadingScreen) || row.launchConsumed
            || !row.owner.session || row.activity == 0xFFFF) continue;
        if (owner.session) { owner = {}; activity = 0xFFFF; return false; }
        owner = row.owner;
        activity = row.activity;
        if (loadingScreenOnly) *loadingScreenOnly = !row.config.suppressLoadingCinematics;
    }
    return owner.session != 0;
}

bool request_movie(Owner owner, std::uint64_t key, unsigned index, bool stop,
                   bool continueSequence, std::int32_t region) noexcept {
    std::lock_guard guard(mutex);
    const Registration* registration = nullptr;
    for (const auto& row : registrations) if (row.owner == owner) registration = &row;
    if (!owner.session || !owner.generation || !registration || !key || region < 0
        || index < 1 || index > registration->config.movieCount
        || (continueSequence && index == registration->config.movieCount)) return false;
    if (stop) {
        if (movie.owner != owner || movie.index != index
            || (status != MovieStatus::queued && status != MovieStatus::preparing
                && status != MovieStatus::playing)) return false;
        movie.stop = true;
        return true;
    }
    if (movie.owner == owner && movie.key == key) {
        return movie.index == index && movie.region == region
            && movie.continueSequence == continueSequence && status != MovieStatus::failed;
    }
    if (status == MovieStatus::queued || status == MovieStatus::preparing
        || status == MovieStatus::playing) return false;
    movie = {owner, registration->config, key, index, region, registration->activity,
             false, continueSequence, true};
    status = MovieStatus::queued;
    return true;
}
MovieStatus movie_status(Owner owner, unsigned index) noexcept {
    std::lock_guard guard(mutex);
    return movie.owner == owner && movie.index == index ? status : MovieStatus::absent;
}
bool movie_request(MovieRequest& output) noexcept {
    std::lock_guard guard(mutex);
    output = movie;
    output.status = status;
    output.registered = false;
    for (const auto& row : registrations)
        if (row.owner == movie.owner && row.owner.session) output.registered = true;
    return movie.key != 0;
}
void movie_observed(Owner owner, std::uint64_t key, MovieStatus observed) noexcept {
    std::lock_guard guard(mutex);
    if (movie.owner != owner || movie.key != key) return;
    if (status == MovieStatus::complete || status == MovieStatus::failed) return;
    if (observed != MovieStatus::failed
        && !(observed == MovieStatus::preparing && status == MovieStatus::queued)
        && !(observed == MovieStatus::playing && status == MovieStatus::preparing)
        && !(observed == MovieStatus::complete && status == MovieStatus::playing)) return;
    status = observed;
}
}
