#pragma once
#include <array>
#include <span>
#include "scriptable_auth_body.h"
#include "../../encoding/bit_writer.h"
#include "../../encoding/bit_reader.h"
namespace sunrise::middleware::bap::activity_message::mission_effect {
inline constexpr std::size_t kBits=186, kBytes=24;
// Native 9EF8A0/9F1F10 remove old attachments on a new revision, then 9EF940
// attaches the authored hop-on to the filter's entities. An absent filter removes all.
inline bool encode(scriptable_auth::Type2LaneClientRef filter, bool enabled,
                   std::int32_t revision, std::span<std::byte> out, std::size_t& written) noexcept {
    if (revision<=0 || out.size()<kBytes || (enabled && (filter.slotType!=34 || filter.slotIndex<0))) return false;
    if (!enabled) filter={};
    encoding::bits::Writer w(out.first(kBytes));
    return w.write(0,1) && w.write(!enabled,1)
        && w.write(0x80000000U,32) && w.write(0x80000000U,32) && w.write(0x80000000U,32)
        && w.write(static_cast<std::uint32_t>(revision)+0x80000000U,32)
        && w.write(filter.registryKey,32) && w.write(static_cast<unsigned>(filter.slotType)+1U,7)
        && w.write(static_cast<std::int32_t>(filter.slotIndex)+32768,16)
        && w.write(0,1) && w.bit_count()==kBits && w.finish(written) && written==kBytes;
}

inline bool validate_filter(std::span<const std::byte> input, std::size_t bits) noexcept {
    if (bits<4 || bits>732 || input.size()!=(bits+7)/8) return false;
    encoding::bits::Reader r(input); std::uint64_t count{},v{},schema{},mode{};
    if (!r.read(4,count) || count>8) return false;
    std::size_t consumed=4;
    for (std::uint64_t i=0;i<count;++i) {
        if (!r.read(1,v) || v!=1 || !r.read(32,schema) || !r.read(2,mode)) return false;
        consumed+=35;
        if (schema==0x8080957DU) { if (mode!=1 && mode!=2) return false; continue; }
        if (schema==0x80809576U) {
            if (!r.read(1,v) || !((mode==2 && v==0) || (mode==1 && v==1))) return false;
            ++consumed;
        } else if (schema!=0x80809579U || mode!=1) return false;
        if (!r.read(32,v) || !r.read(7,v) || v!=(schema==0x80809576U ? 61U : 5U)
            || !r.read(16,v) || v<32768U) return false;
        consumed+=55;
    }
    return consumed==bits && (bits%8==0 || (std::to_integer<unsigned>(input.back()) & ((1U<<(8-bits%8))-1))==0);
}
inline bool validate(std::span<const std::byte> input, std::size_t bits) noexcept {
    if (bits!=kBits || input.size()!=kBytes) return false;
    encoding::bits::Reader r(input); std::uint64_t v{},disabled{},revision{},registry{},type{},index{};
    if (!r.read(1,v)||v!=0||!r.read(1,disabled)) return false;
    for (int i=0;i<3;++i) if (!r.read(32,v)||v!=0x80000000U) return false;
    if (!r.read(32,revision)||revision<=0x80000000U||!r.read(32,registry)||!r.read(7,type)
        ||!r.read(16,index)||!r.read(1,v)||v!=0) return false;
    std::array<std::byte,kBytes> expected{};std::size_t written{};
    if (!encode({static_cast<std::uint32_t>(registry),static_cast<std::int8_t>(type-1),static_cast<std::int16_t>(index-32768)},
                !disabled,static_cast<std::int32_t>(revision-0x80000000U),expected,written)) return false;
    for(std::size_t i=0;i<kBytes;++i)if(expected[i]!=input[i])return false;
    return true;
}
}
