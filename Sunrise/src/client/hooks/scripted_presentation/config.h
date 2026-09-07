#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace sunrise::client::hooks::scripted_presentation {
struct Owner {
    std::uint64_t session{}, generation{};
    bool operator==(const Owner&) const = default;
};
struct Movie {
    std::uint32_t asset{}, header{}, subtitles{}, catalog{}, stream{};
    bool operator==(const Movie&) const = default;
};
struct SurfaceSet {
    std::array<std::uint32_t, 6> definitions{}, buffers{}, containers{};
    bool operator==(const SurfaceSet&) const = default;
};
struct EffectAttachment {
    std::int32_t region{-1};
    std::uint32_t source{}, sourceOffset{}, original{}, replacement{};
};
struct DeliveryChannel {
    std::int32_t region{-1};
    std::uint32_t resource{}, channel{};
};
struct Config {
    std::array<Movie, 8> movies{};
    SurfaceSet surfaces{};
    std::array<EffectAttachment, 8> effects{};
    std::array<DeliveryChannel, 8> deliveries{};
    unsigned movieCount{}, effectCount{}, deliveryCount{};
    bool suppressLoadingCinematics{};
};
struct Registration {
    Owner owner{};
    std::uint16_t activity{0xFFFF};
    Config config{};
};
#ifdef _WIN32
using ActivityReader = void(__fastcall*)(std::uint16_t*);
#else
using ActivityReader = void (*)(std::uint16_t*);
#endif
void activity_reader(ActivityReader reader) noexcept;
void observe_world(std::int32_t region, std::int32_t step) noexcept;
std::uint64_t world_revision() noexcept;
std::int32_t current_region() noexcept;
bool publish(Owner owner, std::uint16_t activity, const Config& config) noexcept;
void remove(Owner owner) noexcept;
std::shared_ptr<const Registration> current() noexcept;
std::shared_ptr<const Registration> find(Owner owner) noexcept;
} // namespace sunrise::client::hooks::scripted_presentation
