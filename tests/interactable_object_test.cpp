#include "../Sunrise/src/server/activity/host_runtime.h"
#include <array>
#include <cassert>
#include <string_view>
#include "../Sunrise/src/middleware/bap/activity_message/interactable_object_auth.h"
#include "../Sunrise/src/server/activity/mission/mission_script_object_sense.h"
#include "../Sunrise/src/middleware/bap/activity_message/sense_observation_packet.h"
namespace sense = sunrise::middleware::bap::activity_message::sense_update;
namespace {
sense::TargetStatus group(const void*, std::uint32_t key, sense::GroupTarget& out) noexcept {
    if (key != 0xF6FFB59E && key != 0x382608B7) return sense::TargetStatus::targetUnavailable;
    out.objectTag=0x80B3CB5E; return sense::TargetStatus::resolved;
}
sense::TargetStatus slot(const void*,const sense::GroupTarget&,std::uint8_t type,
    std::uint16_t index,sense::SlotTarget& out) noexcept {
    if (type != 4 || (index != 19 && index != 31)) return sense::TargetStatus::targetUnavailable;
    out.senseSchema=0x8080992E; return sense::TargetStatus::resolved;
}
}
int main() {
    namespace object=sunrise::middleware::bap::activity_message::interactable_object;
    using namespace sunrise::server::activity::mission;
    std::array<std::byte,object::kBytes> auth{}; std::size_t written{};
    assert(object::encode(1,auth,written) && written==47 && object::validate(auth,375));
    // Native F32CD0 permits use with +704=false and no player filter. F33930
    // maps wire 0/native -1 to false; wire 2/native +1 would hide the prompt.
    sunrise::middleware::encoding::bits::Reader enabled(auth);
    std::uint64_t mode{};
    assert(enabled.skip(285) && enabled.read(2,mode) && mode==0);
    const auto nativeMode = static_cast<int>(mode)-1;
    assert(nativeMode!=0 && !(nativeMode==1));
    auto disabled = auth;
    disabled[35] |= std::byte{4}; // wire mode 2 at bit offset 285
    assert(!object::validate(disabled,375));
    assert(!object::encode(0,auth,written));
    assert(!object::encode(1,std::span(auth).first(46),written));
    auth.back() |= std::byte{1}; assert(!object::validate(auth,375));
    std::array<std::byte,object::kOwnerBytes> ownerAuth{};
    assert(object::encode(3,ownerAuth,written,true) && written==51 && object::validate(ownerAuth,408));
    assert(!object::validate(ownerAuth,375));
    // Accepted delivery retires the carried entity under a newer generation.
    std::array<std::byte,object::kOwnerBytes> consumed{};
    assert(object::encode(4,consumed,written,true,false));
    assert(object::validate(consumed,object::kOwnerBits));
    sunrise::middleware::encoding::bits::Reader retired(consumed);
    std::uint64_t active{}, generation{};
    assert(retired.read(32,generation) && generation==0x80000004ULL);
    assert(retired.skip(32) && retired.read(1,active) && active==0);
    ownerAuth.back() ^= std::byte{1}; assert(!object::validate(ownerAuth,408));
    constexpr std::array<std::string_view,2> fixtures{
        "000000000000000000000000000000003edff6b3c00000241f6ffb59e0b002780000001e000000000000000400000001c04027dba00000000000000080",
        "000000000000000000000000000000003edff6b3c00000241f6ffb59e0b002780000001e000000000000000400000001c04027dbe00000000000000080",
    };
    ObjectInteractionLevel level{};
    const sense::Resolver resolver{nullptr,&group,&slot};
    for (std::size_t i=0;i<fixtures.size();++i) {
        std::array<std::byte,61> packet{};
        const auto size=fixtures[i].size()/2;
        auto nibble=[](char c) { return c<='9' ? c-'0' : c-'a'+10; };
        for (std::size_t j=0;j<size;++j)
            packet[j]=std::byte((nibble(fixtures[i][j*2])<<4)|nibble(fixtures[i][j*2+1]));
        sense::SenseUpdate decoded{}; std::size_t consumed{};
        assert(sense::decode_sense_update(std::span(packet).first(size),resolver,decoded,consumed));
        assert(decoded.decoded.status==sense::DecodeStatus::complete);
        assert(decoded.decoded.objectCount==1 && decoded.decoded.objects[0].deltaBits==199);
        const auto& o=decoded.decoded.objects[0];
        const auto values=std::span(decoded.decoded.values).subspan(o.firstValue,o.valueCount);
        assert(update_object_interaction(level,values,o.schemaRow)==(i==1));
        assert(!update_object_interaction(level,values,o.schemaRow));
        assert(level.generation==1 && level.interacted==(i==1));
        // An unrelated unsupported group must not discard this fully decoded use receipt.
        auto mixed=decoded.decoded;
        mixed.status=sense::DecodeStatus::partial;
        mixed.groupsSkipped=1;
        auto& unknown=mixed.objects[mixed.objectCount++];
        unknown.status=sense::ObjectStatus::unsupportedField;
        unknown.firstValue=static_cast<std::uint32_t>(mixed.valueCount);
        assert(sense::observation_packet(mixed));
        sunrise::server::activity::host::Event event{};
        event.kind = sunrise::server::activity::host::EventKind::senseUpdate;
        event.senseDecodeStatus = mixed.status;
        event.senseSnapshotRetained = sense::observation_packet(mixed);
        assert(event.has_sense_observations());
        mixed.objectsTruncated=true; assert(!sense::observation_packet(mixed));
        event.senseSnapshotRetained = sense::observation_packet(mixed);
        // Framed partial packets can still exceed observation storage. Their input
        // sequence must be consumed without dispatching a nonexistent snapshot.
        assert(event.senseDecodeStatus == sense::DecodeStatus::partial && !event.has_sense_observations());
        event.senseDecodeStatus = sense::DecodeStatus::complete;
        assert(!event.has_sense_observations());
        mixed.objectsTruncated=false; mixed.valuesTruncated=true;
        assert(!sense::observation_packet(mixed));
        mixed.valuesTruncated=false; mixed.objects[0].hasGeneration=false;
        assert(!sense::observation_packet(mixed));
        mixed.objects[0].hasGeneration=true; mixed.status=sense::DecodeStatus::malformed;
        assert(!sense::observation_packet(mixed));
        assert(!sense::decode_sense_update(std::span(packet).first(size-1),resolver,decoded,consumed));
    }
    // SDK 80809ACC flags 0x21: required custom uint64, with no presence bit.
    // Root generation/live state and the native ownership reply, unheld then held.
    constexpr std::array<std::string_view,2> ownerFixtures{
        "000000000000000000000000000000002704c116e00000281382608b70b003f80000001e000000000000000400000001c0404d6600000000000000000000000040",
        "000000000000000000000000000000002704c116e00000281382608b70b003f80000001e000000000000000400000001c0404d6667aa8c0040040040c000000040",
    };
    ObjectInteractionLevel ownerLevel{};
    for (std::size_t i=0; i<ownerFixtures.size(); ++i) {
        std::array<std::byte,65> packet{};
        auto nibble=[](char c) { return c<='9' ? c-'0' : c-'a'+10; };
        for (std::size_t j=0; j<packet.size(); ++j)
            packet[j]=std::byte((nibble(ownerFixtures[i][j*2])<<4)|nibble(ownerFixtures[i][j*2+1]));
        sense::SenseUpdate decoded{}; std::size_t consumed{};
        assert(sense::decode_sense_update(packet,resolver,decoded,consumed));
        assert(decoded.decoded.status==sense::DecodeStatus::complete);
        assert(sense::observation_packet(decoded.decoded));
        const auto& o=decoded.decoded.objects[0];
        assert(o.registryKey==0x382608B7 && o.slotIndex==31 && o.hasGeneration);
        static_cast<void>(update_object_interaction(ownerLevel,
            std::span(decoded.decoded.values).subspan(o.firstValue,o.valueCount),o.schemaRow));
        assert(ownerLevel.generation==1 && ownerLevel.present && ownerLevel.alive && ownerLevel.ownerKnown);
        assert(ownerLevel.hasOwner==(i==1));
        assert(ownerLevel.ownerKey==(i==1 ? 0x9EAA300100100103ULL : 0));
    }
    std::array<sense::DecodedValue,1> values{};
    auto& value=values[0]; value.present=true; value.schemaRow=0x8080992E;
    value.signedValue=2;
    assert(!update_object_interaction(level,values,0x8080992E));
    assert(!level.interactionKnown); // generation-only update cannot reuse the old used latch.
    value.schemaRow=object::kReply; value.unsignedValue=1;
    assert(update_object_interaction(level,values,0x8080992E));
    std::array<sense::DecodedValue,5> ownership{};
    for(auto& v:ownership)v.present=true;
    ownership[0].schemaRow=0x8080992E;ownership[0].signedValue=3;
    ownership[1].schemaRow=0x8080992E;ownership[1].fieldOrdinal=2;ownership[1].unsignedValue=1;
    ownership[2].schemaRow=0x80809ACC;ownership[2].unsignedValue=1;
    ownership[3].schemaRow=0x80809ACC;ownership[3].fieldOrdinal=1;ownership[3].unsignedValue=1234;
    ownership[4].schemaRow=0x8080992E;ownership[4].fieldOrdinal=1;ownership[4].unsignedValue=1;
    static_cast<void>(update_object_interaction(level,ownership,0x8080992E));
    assert(level.generation==3 && level.present && level.alive && level.ownerKnown && level.hasOwner && level.ownerKey==1234);
    ownership[0].signedValue=2;ownership[2].unsignedValue=0;
    static_cast<void>(update_object_interaction(level,ownership,0x8080992E));
    assert(level.hasOwner && level.generation==3);
    ownership[0].signedValue=4;
    static_cast<void>(update_object_interaction(level,std::span(ownership).first(2),0x8080992E));
    assert(!level.ownerKnown && !level.hasOwner && level.ownerKey==0);
    value.schemaRow=0x8080992E; value.signedValue=1;
    assert(!update_object_interaction(level,values,0x8080992E) && level.generation==4);
}
