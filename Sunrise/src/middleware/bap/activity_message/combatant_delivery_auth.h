#pragma once
#include <array>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
namespace sunrise::middleware::bap::activity_message::combatant_delivery {
inline constexpr std::uint32_t kSchema = 0x80807DA1;
inline constexpr std::size_t kBits = 132, kBytes = 17;
struct Request {
    std::uint32_t generation{}, revision{}, registryKey{};
    std::uint16_t squadIndex{};
};
// AB6600 dispatches Auth .7 to the actor's 80807DB4 interface. Harvester component
// 8080670A implements it at 1029330: create passengers in seats, finish after detach.
// The reference MUST be a squad (type 1); its counts must be reserved beforehand.
// Auth .6 remains absent so delivery cannot replace the movement program.
struct SquadReference { std::uint32_t registryKey{}; std::uint16_t squadIndex{}; };
inline constexpr std::size_t kMaxSquads = 8;
inline constexpr std::size_t kMaxBytes = (kBits + 55 * (kMaxSquads - 1) + 7) / 8;
[[nodiscard]] inline bool encode_many(std::uint32_t generation, std::uint32_t revision,
    std::span<const SquadReference> squads, std::span<std::byte> output,
    std::size_t& written, std::size_t& bits) noexcept {
    written = bits = 0;
    if (!generation || generation > 0x7fffffff || !revision || revision > 0x7fffffff
        || squads.empty() || squads.size() > kMaxSquads) return false;
    for (std::size_t i = 0; i < squads.size(); ++i) {
        if (!squads[i].registryKey || squads[i].squadIndex > 32767) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (squads[i].registryKey == squads[j].registryKey && squads[i].squadIndex == squads[j].squadIndex)
                return false;
    }
    std::array<std::byte, kMaxBytes> b{};
    encoding::bits::Writer w(b);
    if (!(w.write(1,1) && w.write(generation,31)
        && w.write(1,2) && w.write(1,3) && w.write(1,1)
        && w.write(0,3) && w.write(1,1) && w.write(squads.size(),4))) return false;
    for (const auto& squad : squads)
        if (!(w.write(squad.registryKey,32) && w.write(2,7) && w.write(squad.squadIndex + 32768U,16))) return false;
    if (!w.write(revision,31)) return false;
    const auto usedBits = w.bit_count(); std::size_t used{};
    if (!w.finish(used) || output.size() < used) return false;
    for (std::size_t i = 0; i < used; ++i) output[i] = b[i];
    written = used; bits = usedBits; return true;
}
[[nodiscard]] inline bool encode(const Request& q, std::span<std::byte> output) noexcept {
    const std::array<SquadReference,1> squads{{{q.registryKey,q.squadIndex}}};
    std::size_t written{}, bits{};
    return output.size() == kBytes && encode_many(q.generation,q.revision,squads,output,written,bits);
}
[[nodiscard]] inline bool validate(std::span<const std::byte> b, std::size_t bits) noexcept {
    if (bits < kBits || b.size() != (bits+7)/8 || b.size() > kMaxBytes) return false;
    encoding::bits::Reader r(b); std::uint64_t v{}, count{};
    if (!(r.read(1,v)&&v==1 && r.read(31,v)&&v>0
        && r.read(2,v)&&v==1 && r.read(3,v)&&v==1 && r.read(1,v)&&v==1
        && r.read(3,v)&&v==0 && r.read(1,v)&&v==1 && r.read(4,count)&&count>0&&count<=kMaxSquads
        && bits == kBits + 55*(count-1))) return false;
    std::array<SquadReference,kMaxSquads> seen{};
    for (std::size_t i=0; i<count; ++i) {
        if (!r.read(32,v) || !v) return false;
        seen[i].registryKey=static_cast<std::uint32_t>(v);
        if (!(r.read(7,v)&&v==2 && r.read(16,v)&&v>=32768)) return false;
        seen[i].squadIndex=static_cast<std::uint16_t>(v-32768);
        for (std::size_t j=0;j<i;++j)
            if (seen[i].registryKey==seen[j].registryKey && seen[i].squadIndex==seen[j].squadIndex) return false;
    }
    return r.read(31,v)&&v>0 && r.read(static_cast<std::uint8_t>(b.size()*8-bits),v)&&v==0;
}
}
