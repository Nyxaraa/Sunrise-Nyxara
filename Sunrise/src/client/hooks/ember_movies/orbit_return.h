#pragma once
#include "ember_movies.h"
namespace sunrise::client::hooks::ember_movies::orbit_return {
void arm(Owner owner) noexcept;
bool active() noexcept;
void poll(int region, int step) noexcept;
}
