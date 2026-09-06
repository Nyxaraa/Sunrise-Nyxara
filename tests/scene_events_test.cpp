#include <array>
#include <cassert>
#include <string_view>
#include "../Sunrise/src/middleware/bap/activity_message/scene_events_auth.h"

namespace scene = sunrise::middleware::bap::activity_message::scene_events;
using Bytes = std::array<std::byte,scene::kMaximumBytes>;

static void field(Bytes& data, std::size_t offset, unsigned width, std::uint64_t value) {
    for (unsigned i=0;i<width;++i) {
        const auto bit=std::byte(1U<<(7-(offset+i)%8));
        if ((value>>(width-i-1))&1) data[(offset+i)/8]|=bit;
        else data[(offset+i)/8]&=~bit;
    }
}
int main() {
    // Schema-derived fixture, independent of the encoder: signed generation, no
    // clear/dependencies/scalar, then four authored FNV-1 explosion event keys.
    constexpr std::array<std::uint32_t,4> keys{0x329EB106,0x633B82E9,0x15A78938,0xF9D55A83};
    Bytes body{};
    std::size_t bytes{},bits{};
    assert(scene::encode(1,keys,body,bytes,bits));
    assert(bytes==26 && bits==202);
    constexpr std::string_view fixture="8000000100000000010ca7ac4198cee0ba4569e24e3e7556a0c0";
    const auto nibble=[](char c) { return unsigned(c<='9' ? c-'0' : c-'a'+10); };
    for (std::size_t i=0;i<bytes;++i)
        assert(std::to_integer<unsigned>(body[i])==((nibble(fixture[2*i])<<4)|nibble(fixture[2*i+1])));
    assert(scene::validate(std::span(body).first(bytes),bits));
    for (const auto& [offset,width,value] : std::array<std::array<std::uint64_t,3>,9>{{
        {0,32,0x80000000U}, {32,1,1}, {33,4,1}, {37,31,1}, {68,6,33},
        {74,32,0}, {74,32,0xFFFFFFFFU}, {106,32,keys[0]}, {207,1,1}}}) {
        auto invalid=body;field(invalid,offset,static_cast<unsigned>(width),value);
        assert(!scene::validate(std::span(invalid).first(bytes),bits));
    }
    assert(!scene::validate(std::span(body).first(bytes-1),bits));
    assert(!scene::validate(std::span(body).first(bytes+1),bits));
    assert(!scene::validate(std::span(body).first(bytes),bits-1));
    assert(!scene::encode(0,keys,body,bytes,bits));
    assert(!scene::encode(-1,keys,body,bytes,bits));
    assert(!scene::encode(1,keys,std::span(body).first(25),bytes,bits));
    assert(!scene::encode(1,std::array<std::uint32_t,2>{1,1},body,bytes,bits));
    std::array<std::uint32_t,33> many{};
    for (std::size_t i=0;i<many.size();++i) many[i]=static_cast<std::uint32_t>(i+1);
    assert(!scene::encode(1,many,body,bytes,bits));
    assert(scene::encode(0x7FFFFFFF,std::span(many).first(32),body,bytes,bits));
    assert(bytes==138 && bits==1098 && scene::validate(std::span(body).first(bytes),bits));
    assert(scene::encode(2,{},body,bytes,bits)); // wipe: new generation, cleared event history
    assert(bytes==10 && bits==74 && scene::validate(std::span(body).first(bytes),bits));
}
