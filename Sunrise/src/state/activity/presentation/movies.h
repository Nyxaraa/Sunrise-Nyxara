#pragma once
#include "config.h"
namespace sunrise::state::activity::presentation {
enum class MovieStatus : std::uint8_t { absent, queued, preparing, playing, complete, failed };
struct MovieRequest {
    Owner owner{};
    Config config{};
    std::uint64_t key{};
    unsigned index{};
    std::int32_t region{-1};
    std::uint16_t activity{0xFFFF};
    bool stop{}, continueSequence{}, registered{};
    MovieStatus status{MovieStatus::absent};
};
// Server producers publish commands; the platform callback consumer owns every native call.
bool request_movie(Owner, std::uint64_t key, unsigned index, bool stop,
                   bool continueSequence, std::int32_t region) noexcept;
MovieStatus movie_status(Owner, unsigned index) noexcept;
bool movie_request(MovieRequest&) noexcept;
void movie_observed(Owner, std::uint64_t key, MovieStatus) noexcept;
}
