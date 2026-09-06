#include <array>
#include <cassert>
#include "../Sunrise/src/middleware/bap/activity_message/darkness_zone_auth.h"
#include "../Sunrise/src/middleware/bap/activity_message/sensor_auth_update.h"
#include "../Sunrise/src/core/logging/log.h"
namespace sunrise::core::log {
void write(Channel, Level, std::string_view) noexcept {}
bool accepts(Channel, Level) noexcept { return false; }
}
// The tested participation/lifetime bodies never use the generic zero-padding arm.
namespace sunrise::middleware::bap::activity_message::sensor_auth_update {
bool pad_bits(encoding::bits::Writer&, std::size_t) noexcept { assert(false);return false; }
}
namespace msg=sunrise::middleware::bap::activity_message::sensor_auth_update;
namespace zone=sunrise::middleware::bap::activity_message::darkness_zone;
namespace bits=sunrise::middleware::encoding::bits;
std::uint64_t read(std::span<const std::byte> body, unsigned offset, unsigned width) {
    bits::Reader reader(body);std::uint64_t value{};
    assert(reader.skip(offset)&&reader.read(width,value));return value;
}
int main() {
    std::array<std::byte,zone::kBytes> body{};bool enabled{};
    assert(zone::encode(true,body)&&zone::decode(body,359,enabled)&&enabled);
    assert(body[0]==std::byte{0x90}); // enabled, no forced wipe, selector0, no countdown (-1).
    for (unsigned i=1;i<body.size();++i) assert(body[i]==std::byte{});
    body[0]|=std::byte{2}; // active wipe timer is not a darkness-only request.
    assert(!zone::decode(body,359,enabled));
    assert(zone::encode(false,body)&&zone::decode(body,359,enabled)&&!enabled);
    assert(!zone::decode(body,358,enabled));
    assert(!zone::encode(true,std::span(body).first(44)));
    for (int seconds=3;seconds>=0;--seconds) {
        assert(zone::encode(true,body,seconds)&&zone::decode(body,359,enabled)&&enabled);
        assert(read(body,4,2)==1 && read(body,6,1)==1);
        const auto elapsed=read(body,7+2*64,64), remaining=read(body,7+3*64,64);
        assert(elapsed+remaining==3*673200 && remaining==std::uint64_t(seconds)*673200);
        assert(read(body,7,64)==elapsed&&read(body,7+64,64)==elapsed);
    }
    assert(!zone::encode(false,body,3)&&!zone::encode(true,body,4));
    msg::Snapshot snapshot{};snapshot.lifetime=3;snapshot.hasRegion=true;snapshot.region=64;
    snapshot.hasDarknessPolicy=true;snapshot.darknessEnabled=true;
    auto lifetime=[&] {
        std::array<std::byte,128> b{};bits::Writer writer(b);std::size_t written{};
        assert(msg::write_auth_body(writer,snapshot,17,false)&&writer.finish(written));
        assert(writer.bit_count()==520&&written==65);
        assert(read(b,0,4)==4); // Preserve the selected activity lifetime.
        return read(b,72,32); // schema 8080991A .5, native +12.
    };
    auto participation=[&] {
        std::array<std::byte,64> b{};bits::Writer writer(b);std::size_t written{};
        assert(msg::write_auth_body(writer,snapshot,13,true)&&writer.finish(written));
        assert(writer.bit_count()==(snapshot.hasDarknessPolicy?240U:224U));
        assert(written==(snapshot.hasDarknessPolicy?30U:28U));
        assert(read(b,176,4)==(snapshot.awaitClientSync?2U:0U));
        assert(read(b,175,1)==1); // No extra "Finding Spawn Location" hold.
        assert(read(b,180,1)==0); // Authored revive delay remains absent.
        assert(read(b,181,1)==snapshot.hasDarknessPolicy);
        const unsigned tail=snapshot.hasDarknessPolicy?198U:182U;
        assert(read(b,tail,2)==0&&read(b,tail+2,8)==128);
        assert(read(b,tail+10,32)==0x80000000U); // No shifted following field.
        return snapshot.hasDarknessPolicy ? read(b,182,16) : 0;
    };
    assert(lifetime()==0x80000008U); // Region64 is bubble8, not bubble64.
    assert(participation()==0x4F80); // Native IEEE half 30 seconds.
    snapshot.awaitClientSync=true;assert(participation()==0x4F80); // Arrival hold stays intact.
    snapshot.darknessEnabled=false;
    assert(lifetime()==0x7fffffffU&&participation()==0x4200); // IEEE half 3 seconds.
    snapshot.awaitClientSync=false;assert(participation()==0x4200);
    snapshot.darknessEnabled=true;assert(participation()==0x4F80);
    snapshot.darknessEnabled=false;
    snapshot.hasDarknessPolicy=false;
    assert(lifetime()==0x80000000U&&participation()==0); // Unscripted missions unchanged.
}
