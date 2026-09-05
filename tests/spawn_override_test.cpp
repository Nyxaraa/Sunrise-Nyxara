#include <algorithm>
#include <cassert>
#include <iostream>

#include "../Sunrise/src/state/activity/destination/activity_destination_spawn_binding.cpp"

namespace data = sunrise::state::build_data;
namespace dest = sunrise::state::activity::destination;
static data::scenarios::Definition layout;
static data::spawn_sets::NameHash spawn;
static bool available = true;
namespace sunrise::state::build_data {
bool find_scenario_layout(std::string_view, scenarios::Definition& out) noexcept {
    out = layout;
    return available;
}
bool find_spawn_sets(std::string_view,
                     std::span<spawn_sets::NameHash> out,
                     std::size_t& count) noexcept {
    count = 1;
    out[0] = spawn;
    return true;
}
} // namespace sunrise::state::build_data
namespace sunrise::core::log {
void write(Channel, Level, std::string_view) noexcept {}
} // namespace sunrise::core::log
int main() {
    dest::DestinationSelection selection{};
    selection.packageName[0] = 'x';
    selection.packageNameLength = 1;
    selection.hasSpawnSetHash = true;
    selection.spawnSetHash = 0x12345678;
    layout.spawnStem[0] = 'x';
    layout.spawnStemLength = 1;
    layout.packageCount = 1;
    layout.packages[0] = 10;
    spawn.value = selection.spawnSetHash;
    spawn.activityPackageCount = 1;
    spawn.activityPackages[0] = 20; // A dependency is absent from the direct package list.
    assert(dest::attachable_spawn_set_hash(selection, 0) == dest::kAbsentSpawnSetHash);
    selection.hasSpawnSetOverride = true;
    selection.spawnSetOverride = spawn.value;
    assert(dest::attachable_spawn_set_hash(selection, 0) == spawn.value);
    selection.spawnSetOverride = dest::kAbsentSpawnSetHash;
    assert(dest::attachable_spawn_set_hash(selection, 0) == dest::kAbsentSpawnSetHash);
    selection.spawnSetOverride = 0;
    assert(dest::attachable_spawn_set_hash(selection, 0) == 0);
    selection.hasSpawnSetOverride = false;
    spawn.activityPackages[0] = 10;
    assert(dest::attachable_spawn_set_hash(selection, 0) == spawn.value);
    spawn.activityPackages[0] = 20;
    spawn.inMapPackage = 1;
    assert(dest::attachable_spawn_set_hash(selection, 0) == spawn.value);
    spawn.inMapPackage = 0;
    available = false;
    assert(dest::attachable_spawn_set_hash(selection, 0) == spawn.value);
    std::cout << "Explicit spawn override precedence and inferred-set filtering passed\n";
}
