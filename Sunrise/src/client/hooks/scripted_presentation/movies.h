#pragma once
#include <cstdint>

#include "config.h"
namespace sunrise::client::hooks::scripted_presentation {
enum class Status : std::uint8_t { absent, queued, preparing, playing, complete, failed };
bool request(Owner owner,
             std::uint64_t request,
             unsigned index,
             bool stop,
             bool continueSequence,
             std::int32_t region) noexcept;
Status status(Owner owner, unsigned index) noexcept;
bool active() noexcept;
bool presenting() noexcept;
void ui_ready(bool ready) noexcept;
void frame_ready(bool ready) noexcept;
void poll(std::int32_t region, std::int32_t step) noexcept;
} // namespace sunrise::client::hooks::scripted_presentation
