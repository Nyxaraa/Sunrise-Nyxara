#pragma once
#include "config.h"
namespace sunrise::state::activity::presentation {
bool publish(Owner owner, const Config& config, std::uint16_t activity = 0xFFFF) noexcept;
void remove(Owner owner) noexcept;
void observe_launch(Owner owner, bool requested, bool arrived) noexcept;
bool loading_mask_request(Owner& owner, std::uint16_t& activity, bool* loadingScreenOnly = nullptr) noexcept;
}
