#pragma once
#include <array>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
namespace sunrise::middleware::bap::activity_message::darkness_zone {
inline constexpr std::uint32_t kSchema=0x808099BF, kClass=0x808099BD;
inline constexpr std::uint8_t kSlotType=35;
inline constexpr std::size_t kBits=359, kBytes=45;
// E4C110 reads .0; E4C0A0 additionally compares lifetime +12 with the current bubble.
// .3=0 selects the native wipe countdown (5011E0/BD4860). The host publishes
// whole-second steps with the timer clamped at the current elapsed value. Native
// 4C8FF0/4C8E90 then expose remaining and elapsed without needing a guessed client
// clock epoch. Native durations use673200 ticks/second (35F080/35FC40).
[[nodiscard]] inline bool encode(bool enabled, std::span<std::byte> output,
    int wipeSeconds=-1) noexcept {
    if (output.size()!=kBytes || wipeSeconds < -1 || wipeSeconds>3 || (!enabled && wipeSeconds>=0)) return false;
    const bool wipe=wipeSeconds>=0;
    std::array<std::byte,kBytes> b{}; encoding::bits::Writer w(b); std::size_t used{};
    if (!(w.write(enabled,1)&&w.write(0,1)&&w.write(1,2)&&w.write(wipe?1:0,2)&&w.write(wipe,1))) return false;
    const std::uint64_t elapsed=wipe ? (3-wipeSeconds)*673200ULL : 0;
    const std::uint64_t remaining=wipe ? wipeSeconds*673200ULL : 0;
    for (const auto value:std::array<std::uint64_t,5>{elapsed,elapsed,elapsed,remaining,0})
        if (!w.write(value,64)) return false;
    if (!(w.write(wipe?0x3F800000U:0U,32)&&w.bit_count()==kBits&&w.finish(used)&&used==kBytes)) return false;
    for (std::size_t i=0;i<kBytes;++i) output[i]=b[i];
    return true;
}
[[nodiscard]] inline bool decode(std::span<const std::byte> body, std::size_t bits, bool& enabled) noexcept {
    if (bits!=kBits||body.size()!=kBytes) return false;
    encoding::bits::Reader r(body); std::uint64_t v{};
    if (!r.read(1,v)) return false;
    const bool active=v!=0;
    if (!(r.read(1,v)&&v==0&&r.read(2,v)&&v==1&&r.read(2,v)&&v<=1)) return false;
    const bool wipe=v==1;
    if (!r.read(1,v) || v!=wipe || (wipe && !active)) return false;
    std::array<std::uint64_t,5> timer{};
    for (auto& word:timer) if (!r.read(64,word)) return false;
    if (wipe) {
        if (timer[0]!=timer[1] || timer[1]!=timer[2] || timer[2]>2019600
            || timer[3]>2019600 || timer[2]+timer[3]!=2019600 || timer[3]%673200 || timer[4]) return false;
    } else for (auto word:timer) if (word) return false;
    if (!(r.read(32,v)&&v==(wipe?0x3F800000U:0U)&&r.read(1,v)&&v==0)) return false;
    enabled=active;return true;
}
}
