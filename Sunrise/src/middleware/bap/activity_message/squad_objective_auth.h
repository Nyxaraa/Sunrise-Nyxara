#pragma once
#include <array>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
namespace sunrise::middleware::bap::activity_message::squad_objective {
inline constexpr std::uint32_t kSchema = 0x80807EC9;
inline constexpr std::size_t kBits = 153, kBytes = 20;
struct Request {
    std::uint32_t registry{}, revision{};
    std::uint16_t index{};
    std::int32_t group{-1};
    bool reserved{};
};
// 4E2A90: root 0 selects the objective, root 13 invalidates its cost calculation,
// root 16 links into an authored task group. -1 requests costs without linking a group.
// Root 15 is explicitly unset; counts/profile/spawn generation remain absent.
[[nodiscard]] inline bool encode(Request q, std::span<std::byte> output) noexcept {
    if (output.size() != kBytes || !q.registry || !q.revision || q.revision > 0x7fffffff
        || q.index > 32767 || q.group < -1 || q.group >= 24) return false;
    std::array<std::byte, kBytes> b{};
    encoding::bits::Writer w(b); std::size_t written{};
    bool ok = w.write(1,1) && w.write(q.registry,32) && w.write(4,7)
        && w.write(q.index + 32768U,16) && w.write(0,12)
        && w.write(1,1) && w.write(q.revision,31) && w.write(0,1)
        && w.write(1,1) && w.write(0,6) && w.write(1,1) && w.write(q.group + 1,5)
        && w.write(0,1) && w.write(2,2) && w.write(q.reserved ? 4 : 3,3)
        && w.write(1,1) && w.write(0x811c9dc5,32)
        && w.bit_count() == kBits && w.finish(written) && written == kBytes;
    if (ok) for (std::size_t i=0;i<kBytes;++i) output[i]=b[i];
    return ok;
}
[[nodiscard]] inline bool validate(std::span<const std::byte> b, std::size_t bits) noexcept {
    if (bits != kBits || b.size()!=kBytes) return false;
    encoding::bits::Reader r(b); std::uint64_t v{}, registry{}, revision{};
    return r.read(1,v)&&v==1 && r.read(32,registry)&&registry
        && r.read(7,v)&&v==4 && r.read(16,v)&&v>=32768
        && r.read(12,v)&&v==0 && r.read(1,v)&&v==1 && r.read(31,revision)&&revision
        && r.read(1,v)&&v==0 && r.read(1,v)&&v==1 && r.read(6,v)&&v==0
        && r.read(1,v)&&v==1 && r.read(5,v)&&v<=24
        && r.read(1,v)&&v==0 && r.read(2,v)&&v==2 && r.read(3,v)&&(v==3 || v==4)
        && r.read(1,v)&&v==1 && r.read(32,v)&&v==0x811c9dc5
        && r.read(7,v)&&v==0;
}
}
