#pragma once
#include <cstdint>
#include <span>
#include "../../../middleware/bap/activity_message/sense_update.h"

namespace sunrise::server::activity::mission {
enum class PlayerLife : std::uint8_t { unknown, alive, dead };
struct PlayerLifeObservation {
    std::uint64_t playerKey{};
    std::int32_t region{-1};
    std::uint8_t known{};
    bool loaded{}, settled{}, ghost{};
    [[nodiscard]] PlayerLife life() const noexcept {
        if (known!=31 || playerKey==0 || playerKey==UINT64_MAX
            || region<0 || region>INT16_MAX || !loaded || !settled) return PlayerLife::unknown;
        return ghost ? PlayerLife::dead : PlayerLife::alive;
    }
};
// A decoded type13 body is a full native snapshot. An absent optional identity
// or region invalidates old life evidence. Clear records on client generation changes.
inline void update_player_life(PlayerLifeObservation& level,
    std::span<const middleware::bap::activity_message::sense_update::DecodedValue> values) noexcept {
    for (const auto& v:values) {
        if (!v.present) {
            if (v.schemaRow==0x808094DD && v.fieldOrdinal==0) { level.playerKey=0;level.known&=~1U; }
            if (v.schemaRow==0x808094E4 && v.fieldOrdinal==0) { level.region=-1;level.known&=~2U; }
            continue;
        }
        if (v.schemaRow==0x808094DD && v.fieldOrdinal==0) {
            level.playerKey=v.unsignedValue; level.known|=1;
        } else if (v.schemaRow==0x808094E4) {
            switch (v.fieldOrdinal) {
            case 0: level.region=static_cast<std::int32_t>(v.signedValue);level.known|=2;break;
            case 2: level.loaded=v.unsignedValue!=0;level.known|=4;break;
            case 4: level.settled=v.unsignedValue!=0;level.known|=8;break;
            case 5: level.ghost=v.unsignedValue!=0;level.known|=16;break;
            default: break;
            }
        }
    }
}
struct FireteamLife {
    std::uint16_t alive{},dead{},unknown{};
    [[nodiscard]] bool all_dead() const noexcept { return dead!=0 && alive==0 && unknown==0; }
    void add(PlayerLife life) noexcept {
        if (life==PlayerLife::alive) ++alive;
        else if (life==PlayerLife::dead) ++dead;
        else ++unknown;
    }
    bool operator==(const FireteamLife&) const = default;
};
}
