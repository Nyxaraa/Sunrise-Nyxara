#pragma once
#include "ember_movies.h"
namespace sunrise::client::hooks::ember_movies {
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
