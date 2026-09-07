#pragma once
#include "movies.h"
namespace sunrise::client::sdk::presentation::orbit_return {
bool request(Owner owner, int region) noexcept;
bool active() noexcept;
void poll(int region, int step) noexcept;
} // namespace sunrise::client::sdk::presentation::orbit_return
