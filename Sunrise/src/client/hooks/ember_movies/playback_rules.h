#pragma once
#include "ember_movies.h"
namespace sunrise::client::hooks::ember_movies {
// E15D50's gameplay choices -> native cinematic_overlay. Category 4 alone
// does not distinguish playback from loading: 1312540 maps 21h -> window 26
// (cinematic_overlay), but 22h -> window 29 (loading) at 13126E0.
constexpr int movie_ui_state(int requested,bool presenting) noexcept {
    return presenting && (requested==0x2B || requested==0x2D || requested==0x2F)
        ? 0x21 : requested;
}
constexpr bool can_chain_movies(Owner current,Owner next,Owner firstCompleted,
                                 unsigned previous,unsigned requested,Status state) noexcept {
    return current.session && current.generation && current==next && current==firstCompleted
        && previous==1 && requested==2 && state==Status::complete;
}
// A queued request and decoder preparation do not prove rendered playback.
struct Playback {
    bool seen{};
    Status observe(bool exactAsset, int decoderState, bool busy) noexcept {
        if (!exactAsset) return seen ? Status::failed : Status::preparing;
        if (decoderState==5) seen=true;
        if (!busy) {
            if (decoderState!=0 && decoderState!=6) return Status::failed;
            return seen ? Status::complete : Status::preparing;
        }
        return seen ? Status::playing : Status::preparing;
    }
};
}
