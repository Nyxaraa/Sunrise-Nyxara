#include <cassert>
#include <array>
#include "../Sunrise/src/middleware/bap/activity_message/activity_sense_update_decoder.cpp"
#include "../Sunrise/src/middleware/bap/activity_message/sensor_auth_update.h"
#include "../Sunrise/src/server/activity/mission/mission_script_player_sense.h"
#include "../Sunrise/src/state/activity/membership/definition.h"
#include "../Sunrise/src/core/logging/log.h"
namespace sunrise::core::log {
void write(Channel,Level,std::string_view) noexcept {}
bool accepts(Channel,Level) noexcept {return false;}
}
namespace sunrise::middleware::bap::activity_message::sensor_auth_update {
bool pad_bits(encoding::bits::Writer&,std::size_t) noexcept {assert(false);return false;}
}
namespace sense=sunrise::middleware::bap::activity_message::sense_update;
namespace auth=sunrise::middleware::bap::activity_message::sensor_auth_update;
namespace bits=sunrise::middleware::encoding::bits;
using namespace sunrise::server::activity::mission;
int main() {
    auth::Snapshot snapshot{};snapshot.hasRegion=true;snapshot.region=8;
    snapshot.playerKey=0x9EAA300100100102ULL;snapshot.hasDarknessPolicy=true;snapshot.darknessEnabled=true;
    std::array<std::byte,64> body{};bits::Writer writer(body);std::size_t written{};
    // Auth and Sense share the exact nested native schema layouts. Use the
    // independently existing Auth producer to verify the new full-body decoder.
    assert(auth::write_auth_body(writer,snapshot,13,true)&&writer.finish(written));
    PlayerLifeObservation level{};
    for (int dead=0;dead<=1;++dead) {
        // Schema808094E4: optional region(33), absent field1(1), flags2..5.
        if (dead) body[4]|=std::byte{0x04};
        bits::Reader source{std::span(body).first(written)};
        sense::Reader reader(source,writer.bit_count(),written*8);
        sense::DecodedPacket packet{};sense::DecodedObject object{};sense::Values values(packet,&object);
        assert(sense::decode_player(reader,values)==sense::NativeStatus::complete);
        assert(reader.left()==0);
        update_player_life(level,std::span(packet.values).first(packet.valueCount));
        assert(level.playerKey==snapshot.playerKey && level.region==8);
        assert(level.life()==(dead?PlayerLife::dead:PlayerLife::alive));
        bits::Reader shortSource{std::span(body).first(written)};
        sense::Reader shortReader(shortSource,writer.bit_count()-1,written*8);
        assert(sense::decode_player(shortReader,values)==sense::NativeStatus::malformed);
    }
    FireteamLife party{};assert(!party.all_dead());
    party.add(PlayerLife::dead);assert(party.all_dead());
    party.add(PlayerLife::alive);assert(!party.all_dead());
    party={};party.add(PlayerLife::dead);party.add(PlayerLife::unknown);assert(!party.all_dead());
    party={};party.add(PlayerLife::dead);party.add(PlayerLife::dead);assert(party.all_dead());
    level.loaded=false;assert(level.life()==PlayerLife::unknown);
    level.loaded=true;level.region=64;assert(level.life()!=PlayerLife::unknown);
    level.loaded=true;level.settled=false;assert(level.life()==PlayerLife::unknown);
    level.settled=true;level.region=-1;assert(level.life()==PlayerLife::unknown);
    level={};assert(level.life()==PlayerLife::unknown);
    using namespace sunrise::state::activity::membership;
    HardWipeState wipe{};wipe.active=true;wipe.host.state=1;wipe.host.opaqueByte=7;
    wipe.observe({4,6,0});assert(wipe.host.state==1); // Prior request cannot release this wipe.
    wipe.observe({0,7,0});assert(wipe.active && wipe.host.state==1);
    wipe.observe({2,7,0});assert(wipe.host.state==1);
    wipe.observe({4,7,0});assert(wipe.host.state==1 && wipe.active);
    wipe.release();assert(wipe.host.state==4);
    wipe.observe({4,7,0});assert(wipe.active);
    wipe.observe({0,7,0});assert(!wipe.active);
    wipe.observe({4,7,0});assert(!wipe.active); // Duplicate completed receipt cannot rearm.
}
