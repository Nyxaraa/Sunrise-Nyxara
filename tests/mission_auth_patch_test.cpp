#include <array>
#include <cassert>
#include <string_view>
#include "../Sunrise/src/middleware/bap/activity_message/mission_auth_patch.h"
#include "../Sunrise/src/middleware/bap/activity_message/squad_auth_body.h"
#include "../Sunrise/src/middleware/bap/activity_message/squad_objective_auth.h"
#include "../Sunrise/src/middleware/bap/activity_message/combatant_path_auth.h"
#include "../Sunrise/src/middleware/bap/activity_message/combatant_delivery_auth.h"
#include "../Sunrise/src/middleware/bap/activity_message/combatant_retire_auth.h"
#include "../Sunrise/src/middleware/bap/activity_message/combatant_action_auth.h"
namespace message = sunrise::middleware::bap::activity_message;
namespace patch = message::mission_auth_patch;
using Bytes = std::array<std::byte,patch::kCapacity>;
// Read a native root's payload, independently of the composition decision.
std::uint64_t value(std::span<const std::byte> body, patch::Field field, unsigned width,
                    bool optional=true, unsigned skip=0) {
    sunrise::middleware::encoding::bits::Reader r(body); std::uint64_t v{};
    assert(r.skip(field.offset+(optional?1:0)+skip) && r.read(width,v)); return v;
}
void transport() {
    namespace path=message::combatant_path;
    namespace cargo=message::combatant_delivery;
    namespace retire=message::combatant_retire;
    std::array<std::byte,path::kBytes> flight{};
    std::array<std::byte,cargo::kBytes> delivery{};
    std::array<std::byte,retire::kBytes> disabled{};
    assert(path::encode({1,1,0xf6ffb59e,115},flight));
    assert(cargo::encode({1,1,0xf6ffb59e,23},delivery));
    Bytes retained{}; std::size_t written{},bits{};
    assert(patch::compose(path::kSchema,flight,path::kBits,delivery,cargo::kBits,retained,written,bits));
    // Independent native packet fixture: spawn + kind3 entry path + one passenger squad.
    constexpr std::string_view fixture="800000014c80000001001a3edff6b3cee01cc071f6ffb59e05002e00000004";
    assert(bits==246 && written==31);
    auto nibble=[](char c) { return c<='9'?c-'0':c-'a'+10; };
    for (std::size_t i=0;i<written;++i)
        assert(std::to_integer<unsigned>(retained[i])==unsigned((nibble(fixture[2*i])<<4)|nibble(fixture[2*i+1])));
    Bytes reversed{}; std::size_t reverseBytes{},reverseBits{};
    assert(patch::compose(path::kSchema,delivery,cargo::kBits,flight,path::kBits,reversed,reverseBytes,reverseBits));
    assert(reversed==retained && reverseBytes==written && reverseBits==bits);
    assert(path::encode({1,2,0xf6ffb59e,116},flight));
    assert(patch::compose(path::kSchema,std::span(retained).first(written),bits,
                          flight,path::kBits,retained,written,bits)); // in-place estate update
    patch::Layout layout{};
    assert(patch::parse(path::kSchema,std::span(retained).first(written),bits,layout));
    assert(value(retained,layout.fields[0],31)==1);
    assert(value(retained,layout.fields[6],31)==2);
    assert(value(retained,layout.fields[7],31,true,4+55)==1); // exit preserves delivery revision
    namespace action = message::combatant_action;
    std::array<std::byte,action::kBytes> animation{};
    assert(action::encode({1,3,0x811c9dc5,0x7b0d3643},animation));
    assert(action::validate(animation,action::kBits));
    assert(!action::validate(animation,action::kBits-1));
    auto badPadding = animation; badPadding.back() |= std::byte{1};
    assert(!action::validate(badPadding,action::kBits));
    assert(!action::encode({0,3,0x811c9dc5,0x7b0d3643},badPadding));
    assert(!action::encode({1,0,0x811c9dc5,0x7b0d3643},badPadding));
    assert(!action::encode({1,3,0x811c9dc5,0x811c9dc5},badPadding));
    assert(patch::compose(path::kSchema,std::span(retained).first(written),bits,
                          animation,action::kBits,retained,written,bits));
    assert(patch::parse(path::kSchema,std::span(retained).first(written),bits,layout));
    assert(value(retained,layout.fields[0],31)==1); // no actor recreation
    assert(value(retained,layout.fields[6],31)==3);
    assert(value(retained,layout.fields[6],4,true,31+6+6+1)==10); // kind 9
    assert(value(retained,layout.fields[6],32,true,31+6+6+1+4+2)==0x811c9dc5);
    assert(value(retained,layout.fields[6],32,true,31+6+6+1+4+2+32)==0x7b0d3643);
    assert(value(retained,layout.fields[7],31,true,4+55)==1); // delivery remains completed
    assert(retire::encode(2,disabled));
    assert(patch::compose(path::kSchema,std::span(retained).first(written),bits,
                          disabled,retire::kBits,retained,written,bits));
    assert(patch::parse(path::kSchema,std::span(retained).first(written),bits,layout));
    assert(value(retained,layout.fields[0],31)==2); // AB71E0 lifecycle reset
    assert(value(retained,layout.fields[3],1,false)==0);
    assert(value(retained,layout.fields[7],4)==0); // retired ships cannot re-request passengers
    assert(value(retained,layout.fields[7],31,true,4)==2);
    assert(value(retained,layout.fields[6],31)==3);
    const auto good=retained;
    assert(!patch::compose(path::kSchema,{},0,std::span(delivery).first(16),cargo::kBits,retained,written,bits));
    assert(retained==good);
    assert(!patch::compose(path::kSchema,{},0,flight,path::kBits,std::span(retained).first(1),written,bits));
    assert(retained==good);
}
void squad() {
    namespace spawn=message::squad_auth;
    namespace objective=message::squad_objective;
    std::array<std::int32_t,2> counts{3,1};
    Bytes placement{},retained{}; std::size_t placementBytes{},placementBits{},written{},bits{};
    assert(spawn::encode({counts,1,spawn::Mode::reserve},{},placement,placementBytes,placementBits));
    std::array<std::byte,objective::kBytes> ai{};
    assert(objective::encode({0xf6ffb59e,1,15,-1,true},ai));
    assert(patch::compose(spawn::kSchema,std::span(placement).first(placementBytes),placementBits,
                          ai,objective::kBits,retained,written,bits));
    for (int group : {0,5,12,-1}) {
        assert(objective::encode({0xf6ffb59e,1,15,group,true},ai));
        assert(patch::compose(spawn::kSchema,std::span(retained).first(written),bits,
                              ai,objective::kBits,retained,written,bits));
        patch::Layout layout{};
        assert(patch::parse(spawn::kSchema,std::span(retained).first(written),bits,layout));
        assert(value(retained,layout.fields[0],32)==0xf6ffb59e);
        assert(value(retained,layout.fields[3],4)==2);
        assert(value(retained,layout.fields[3],32,true,4)==0x80000003);
        assert(value(retained,layout.fields[3],32,true,36)==0x80000001);
        assert(value(retained,layout.fields[6],31)==1); // AI never creates a new spawn generation
        assert(value(retained,layout.fields[18],2,false)==2); // populated: native mode 1
        assert(value(retained,layout.fields[16],5)==unsigned(group+1));
        assert(value(retained,layout.fields[19],3,false)==4); // reserved mode survives
    }
    counts={0,0};
    assert(spawn::encode({counts,2,spawn::Mode::reserve},{1,true},placement,placementBytes,placementBits));
    assert(patch::compose(spawn::kSchema,std::span(retained).first(written),bits,
                          std::span(placement).first(placementBytes),placementBits,retained,written,bits));
    patch::Layout layout{};
    assert(patch::parse(spawn::kSchema,std::span(retained).first(written),bits,layout));
    assert(value(retained,layout.fields[3],32,true,4)==0x80000000);
    assert(value(retained,layout.fields[3],32,true,36)==0x80000000);
    assert(value(retained,layout.fields[6],31)==2);
    assert(value(retained,layout.fields[18],2,false)==1); // zero counts: native destruction, not death
    assert(value(retained,layout.fields[0],32)==0xf6ffb59e);
    // A checkpoint's later spawn restores the populated mode and advances generation;
    // a composition with empty categories must not be mistaken for retirement.
    counts={0,1};
    assert(spawn::encode({counts,3,spawn::Mode::mode2},{2,true},placement,placementBytes,placementBits));
    assert(patch::compose(spawn::kSchema,std::span(retained).first(written),bits,
                          std::span(placement).first(placementBytes),placementBits,retained,written,bits));
    assert(patch::parse(spawn::kSchema,std::span(retained).first(written),bits,layout));
    assert(value(retained,layout.fields[6],31)==3);
    assert(value(retained,layout.fields[18],2,false)==2);
    assert(value(retained,layout.fields[3],32,true,36)==0x80000001);
    // A full eight-category squad plus objective exceeds the old spawn-only lease.
    const std::array<std::int32_t,8> largeCounts{1,1,1,1,1,1,1,1};
    assert(spawn::encode({largeCounts,1,spawn::Mode::mode2},{},placement,placementBytes,placementBits));
    assert(patch::compose(spawn::kSchema,std::span(placement).first(placementBytes),placementBits,
                          ai,objective::kBits,retained,written,bits));
    assert(written > spawn::kMaximumByteCount && written <= spawn::kMaximumRetainedByteCount);
}
void manifest_transport() {
    namespace cargo = sunrise::middleware::bap::activity_message::combatant_delivery;
    namespace path = sunrise::middleware::bap::activity_message::combatant_path;
    namespace patch = sunrise::middleware::bap::activity_message::mission_auth_patch;
    const std::array<cargo::SquadReference,2> manifest{{{0xf6ffb59e,23},{0xf6ffb59e,25}}};
    std::array<std::byte,cargo::kMaxBytes> cargoBody{};
    std::array<std::byte,path::kBytes> pathBody{};
    Bytes retained{}; std::size_t n{},bits{},written{},retainedBits{};
    assert(cargo::encode_many(1,1,manifest,cargoBody,n,bits));
    assert(path::encode({1,1,0xf6ffb59e,115},pathBody));
    assert(patch::compose(path::kSchema,pathBody,path::kBits,std::span(cargoBody).first(n),bits,
        retained,written,retainedBits));
    assert(path::encode({1,2,0xf6ffb59e,116},pathBody));
    assert(patch::compose(path::kSchema,std::span(retained).first(written),retainedBits,pathBody,path::kBits,
        retained,written,retainedBits));
    patch::Layout layout{};
    assert(patch::parse(path::kSchema,std::span(retained).first(written),retainedBits,layout));
    assert(value(retained,layout.fields[6],31)==2);
    assert(value(retained,layout.fields[7],4)==2);
    assert(value(retained,layout.fields[7],32,true,4)==0xf6ffb59e);
    assert(value(retained,layout.fields[7],16,true,4+32+7)==32768+23);
    assert(value(retained,layout.fields[7],16,true,4+55+32+7)==32768+25);
}
int main() { transport(); squad(); manifest_transport(); }
