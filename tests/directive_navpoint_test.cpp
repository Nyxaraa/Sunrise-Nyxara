#include <array>
#include <cassert>
#include "../Sunrise/src/middleware/bap/activity_message/scriptable_auth_body.h"
#include "../Sunrise/src/middleware/encoding/bit_reader.h"
namespace auth = sunrise::middleware::bap::activity_message::scriptable_auth;
namespace bits = sunrise::middleware::encoding::bits;
int main() {
    std::array<std::byte,auth::kType68ByteCount> body{};
    auth::Type68Preset preset{.nameHash=0x2700C0C5,.elementIndex=2,
        .navpoint={0x12345678,47,1}};
    std::size_t written{};
    auto read=[&](std::size_t offset, unsigned width) {
        bits::Reader reader(body);std::uint64_t value{};
        while(offset) { const auto step=static_cast<std::uint8_t>(offset>64?64:offset);
            assert(reader.read(step,value));offset-=step; }
        assert(reader.read(static_cast<std::uint8_t>(width),value));return value;
    };
    assert(auth::encode_type68(preset,body,written));
    assert(written==body.size() && auth::validate_type68_body(body,auth::kType68BitCount));
    // Root refs110; entry header66 + timer353 + counters128 + state2 + ref55 + mode3.
    constexpr auto marker=110+66+353+128+2+55+3;
    assert(read(marker-3,3)==3); // Native mode2 (signed bias1), direct route in destination bubble.
    assert(read(marker,32)==0x12345678 && read(marker+32,7)==47 && read(marker+39,16)==32769);
    assert(read(marker+55,32)==0x811C9DC5); // No alternate positional anchor.
    assert(read(marker+110,32)==0x811C9DC5); // No hashed fallback lookup.
    for (unsigned i=1;i<4;++i) assert(read(marker+i*239,32)==0x811C9DC5);
    preset.navpoint={};assert(auth::encode_type68(preset,body,written));
    assert(read(marker,32)==0x811C9DC5 && read(marker+39,16)==32767);
    assert(read(marker-3,3)==1 && auth::validate_type68_body(body,auth::kType68BitCount));
    preset.audience={0x12345678,70,104};
    assert(auth::encode_type68(preset,body,written));
    assert(read(0,32)==0x12345678 && read(32,7)==71 && read(39,16)==32872);
    assert(auth::validate_type68_body(body,auth::kType68BitCount));
    preset.audience.slotType=4;assert(!auth::encode_type68(preset,body,written));
    preset.audience={};
    preset.navpoint={0x12345678,2,1};assert(!auth::encode_type68(preset,body,written));
    preset.navpoint={0x811C9DC5,47,1};assert(!auth::encode_type68(preset,body,written));
    preset.navpoint={0x12345678,47,1};preset.visible=false;
    assert(!auth::encode_type68(preset,body,written));
}
