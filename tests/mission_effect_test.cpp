#include <cassert>
#include "../Sunrise/src/middleware/bap/activity_message/mission_effect_auth.h"
int main(){
 namespace effect=sunrise::middleware::bap::activity_message::mission_effect;
 namespace auth=sunrise::middleware::bap::activity_message::scriptable_auth;
 std::array<std::byte,effect::kBytes> bytes{};std::size_t written{};
 assert(effect::encode({0x12345678,34,117},true,3,bytes,written));
 assert(written==24&&effect::validate(bytes,186));
 bytes.back()|=std::byte{1};assert(!effect::validate(bytes,186));
 assert(effect::encode({},false,4,bytes,written)&&effect::validate(bytes,186));
 assert(!effect::encode({},true,4,bytes,written));
 assert(!effect::encode({1,34,1},true,0,bytes,written));
 auth::Type34Body filter{};filter.count=2;
 filter.predicates[0]=auth::Type34ModeOnlyB{0};
 filter.predicates[1]=auth::Type34ModeFlagSlotRef{1,false,{0x98765432,60,116}};
 std::array<std::byte,auth::kType34MaximumByteCount> out{};std::size_t bits{};
 assert(auth::encode_type34(filter,out,written,bits));
 assert(bits==130&&effect::validate_filter(std::span(out).first(written),bits));
 assert(!effect::validate_filter(std::span(out).first(written-1),bits));
 filter.predicates[1]=auth::Type34ModeFlagSlotRef{0,false,{1,60,1}}; // Native unreachable add-outside combination.
 assert(auth::encode_type34(filter,out,written,bits));
 assert(!effect::validate_filter(std::span(out).first(written),bits));
 filter.count=6;
 for (int i=0;i<5;++i) filter.predicates[i]=auth::Type34ModeFlagSlotRef{0,true,{1,60,static_cast<std::int16_t>(i+3)}};
 filter.predicates[5]=auth::Type34ModeOnlyB{1};
 assert(auth::encode_type34(filter,out,written,bits));
 assert(bits==494 && effect::validate_filter(std::span(out).first(written),bits));
 filter.count=1;filter.predicates[0]=auth::Type34ModeSlotRefC{0,{1,4,25}};
 assert(auth::encode_type34(filter,out,written,bits));
 assert(bits==94&&effect::validate_filter(std::span(out).first(written),bits));
}
