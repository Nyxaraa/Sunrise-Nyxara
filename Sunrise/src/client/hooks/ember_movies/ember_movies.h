#pragma once
#include <cstdint>
namespace sunrise::client::hooks::ember_movies {
// Only Ember's two packaged pre-rendered bookends are admitted by this bridge.
struct Owner { std::uint64_t session{}, generation{}; bool operator==(const Owner&) const = default; };
enum class Status : std::uint8_t { absent, queued, preparing, playing, complete, failed };
bool request(Owner owner, std::uint64_t request, unsigned index, bool stop) noexcept;
Status status(Owner owner, unsigned index) noexcept;
bool active() noexcept;
bool presenting() noexcept;
void ui_ready(bool ready) noexcept;
void frame_ready(bool ready) noexcept;
void poll(std::int32_t region, std::int32_t step) noexcept;
}
