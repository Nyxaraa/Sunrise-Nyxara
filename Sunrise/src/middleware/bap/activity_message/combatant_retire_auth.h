#pragma once
#include <array>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
namespace sunrise::middleware::bap::activity_message::combatant_retire {
inline constexpr std::uint32_t kSchema = 0x80807DA1;
inline constexpr std::size_t kBits = 77, kBytes = 10;
// The live Auth callback AB6E20 -> AB71E0 retires only on a NEW generation.
// Enabled=false alone only prevents creation in AB7350; it does not remove the actor.
// Clear delivery .7 in the same update: AB7130 must not replay old cargo after reset.
// Root .1 mode0 takes native detach + entity destruction (AB89A0 -> 56A8F0).
[[nodiscard]] inline bool encode(std::uint32_t generation, std::span<std::byte> output) noexcept {
    if (output.size() != kBytes || !generation || generation > 0x7fffffff) return false;
    std::array<std::byte, kBytes> b{};
    encoding::bits::Writer w(b); std::size_t written{};
    const bool ok = w.write(1,1) && w.write(generation,31)
        && w.write(1,2) && w.write(1,3) && w.write(0,1) && w.write(0,3) && w.write(1,1)
        && w.write(0,4) && w.write(generation,31)
        && w.bit_count() == kBits && w.finish(written) && written == kBytes;
    if (ok) for (std::size_t i=0;i<kBytes;++i) output[i]=b[i];
    return ok;
}
[[nodiscard]] inline bool validate(std::span<const std::byte> b, std::size_t bits) noexcept {
    if (b.size()!=kBytes || bits!=kBits) return false;
    encoding::bits::Reader r(b); std::uint64_t v{};
    return r.read(1,v)&&v==1 && r.read(31,v)&&v>0 && r.read(2,v)&&v==1
        && r.read(3,v)&&v==1 && r.read(1,v)&&v==0 && r.read(3,v)&&v==0
        && r.read(1,v)&&v==1 && r.read(4,v)&&v==0 && r.read(31,v)&&v>0
        && r.read(3,v)&&v==0;
}
}
