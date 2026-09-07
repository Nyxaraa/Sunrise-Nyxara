#include "config.h"

#include <atomic>
#include <mutex>

#include "config_rules.h"

namespace sunrise::client::sdk::presentation {
namespace {
std::mutex mutex;
std::array<std::shared_ptr<const Registration>, 16> registrations;
std::atomic<ActivityReader> reader{};
std::atomic<std::int32_t> region{-1};
std::atomic<std::int32_t> worldStep{-1};
std::atomic<std::uint64_t> worldRevision{};
} // namespace
void activity_reader(ActivityReader value) noexcept {
    reader.store(value);
}
void observe_world(std::int32_t value, std::int32_t step) noexcept {
    const auto previousRegion=region.exchange(value);
    const auto previousStep=worldStep.exchange(step);
    if (previousRegion!=value || previousStep!=step) worldRevision.fetch_add(1);
}
std::uint64_t world_revision() noexcept { return worldRevision.load(); }
std::int32_t current_region() noexcept {
    return region.load();
}
bool publish(Owner owner, std::uint16_t activity, const Config& config) noexcept {
    if (!owner.session || !owner.generation || activity == 0xFFFF || !valid_config(config))
        return false;
    try {
        auto value = std::make_shared<Registration>(Registration{owner, activity, config});
        std::lock_guard guard(mutex);
        for (auto& row : registrations) {
            if (row && row->owner == owner) {
                row = std::move(value);
                return true;
            }
        }
        for (auto& row : registrations) {
            if (!row) {
                row = std::move(value);
                return true;
            }
        }
    } catch (...) {
        return false;
    }
    return false;
}
void remove(Owner owner) noexcept {
    std::lock_guard guard(mutex);
    for (auto& row : registrations)
        if (row && row->owner == owner) row.reset();
}
std::shared_ptr<const Registration> find(Owner owner) noexcept {
    std::lock_guard guard(mutex);
    for (const auto& row : registrations)
        if (row && row->owner == owner) return row;
    return {};
}
std::shared_ptr<const Registration> current() noexcept {
    const auto read = reader.load();
    if (!read) return {};
    std::uint16_t activity = 0xFFFF;
    read(&activity);
    if (activity == 0xFFFF) return {};
    std::lock_guard guard(mutex);
    std::shared_ptr<const Registration> selected;
    for (const auto& row : registrations) {
        if (!row || row->activity != activity) continue;
        // An ambiguous local owner must not affect another activity instance.
        if (selected) return {};
        selected = row;
    }
    return selected;
}
} // namespace sunrise::client::sdk::presentation
