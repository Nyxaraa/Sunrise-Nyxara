#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace sunrise::middleware::bap::activity_message::mission_auth_patch {
// Native sensor overrides replace the full Auth object. An absent root field in a
// mission API call means preserve it, so materialize against the last staged body.
// This deliberately supports only the squad and named-transport layouts these APIs
// emit. Unsupported nested programs are refused rather than losing retained state.
inline constexpr std::size_t kCapacity = 256;
struct Field { std::size_t offset{}, bits{}; bool present{}; };
struct Layout { std::array<Field,21> fields{}; std::size_t count{}; };
[[nodiscard]] inline bool parse(std::uint32_t schema, std::span<const std::byte> body,
                                std::size_t bits, Layout& out) noexcept {
    if (body.empty() || body.size()>kCapacity || body.size()!=(bits+7)/8) return false;
    const bool squad = schema==0x80807EC9U;
    if (!squad && schema!=0x80807DA1U) return false;
    encoding::bits::Reader r(body); out={}; out.count=squad?21:8;
    auto position = [&] { return body.size()*8-r.remaining_bits(); };
    for (std::size_t i=0;i<out.count;++i) {
        auto& f=out.fields[i]; f.offset=position();
        const bool optional=squad?(i!=18 && i!=19):(i==0 || i>=4);
        std::uint64_t v=1;
        if (optional && !r.read(1,v)) return false;
        f.present=v!=0;
        if (f.present) {
            std::size_t width=0;
            if (squad) {
                if (i==0 || i==1 || (i>=9 && i<=12)) width=55;
                else if (i==2) return false;
                else if (i==3 || i==4) {
                    if (!r.read(4,v) || v>8) return false;
                    width=32*v;
                } else if (i==5) width=13;
                else if (i==6 || i==13 || i==14 || i==17) width=31;
                else if (i==7 || i==8 || i==20) width=32;
                else if (i==15) width=6;
                else if (i==16) width=5;
                else if (i==18) width=2;
                else if (i==19) width=3;
            } else {
                if (i==0) width=31;
                else if (i==1) width=2;
                else if (i==2) width=3;
                else if (i==3) width=1;
                else if (i==4 || i==5) return false;
                else if (i==6) {
                    if (!r.skip(31+6) || !r.read(6,v) || v!=1
                        || !r.read(1,v) || v!=1 || !r.read(4,v) || (v!=4 && v!=10)) return false;
                    const auto kind = v;
                    if (!r.read(2,v) || v!=1) return false;
                    width=kind==4 ? 64 : 162; // kind-3 ClientRef, marker byte, enabled bit
                } else if (i==7) {
                    if (!r.read(4,v) || v>8) return false;
                    width=55*v+31;
                }
            }
            if (!r.skip(width)) return false;
        }
        f.bits=position()-f.offset;
    }
    std::uint64_t padding{};
    return position()==bits && r.read(static_cast<std::uint8_t>(body.size()*8-bits),padding) && padding==0;
}
[[nodiscard]] inline bool copy_field(encoding::bits::Writer& w, std::span<const std::byte> body,
                                     Field f) noexcept {
    encoding::bits::Reader r(body);
    if (!r.skip(f.offset)) return false;
    while (f.bits) {
        const auto n=static_cast<std::uint8_t>(f.bits>64?64:f.bits);
        std::uint64_t v{};
        if (!r.read(n,v) || !w.write(v,n)) return false;
        f.bits-=n;
    }
    return true;
}
[[nodiscard]] inline bool compose(std::uint32_t schema,
    std::span<const std::byte> previous, std::size_t previousBits,
    std::span<const std::byte> patch, std::size_t patchBits,
    std::span<std::byte> output, std::size_t& written, std::size_t& bits) noexcept {
    written=bits=0;
    Layout old{}, next{};
    if (!parse(schema,patch,patchBits,next)
        || (!previous.empty() && !parse(schema,previous,previousBits,old))) return false;
    std::array<std::byte,kCapacity> staged{};
    encoding::bits::Writer w(staged);
    for (std::size_t i=0;i<next.count;++i) {
        const bool keep=!previous.empty() && !next.fields[i].present && old.fields[i].present;
        if (!copy_field(w,keep?previous:patch,keep?old.fields[i]:next.fields[i])) return false;
    }
    const auto usedBits=w.bit_count(); std::size_t used{};
    if (!w.finish(used) || output.size()<used) return false;
    for (std::size_t i=0;i<used;++i) output[i]=staged[i];
    written=used; bits=usedBits; return true;
}
}
