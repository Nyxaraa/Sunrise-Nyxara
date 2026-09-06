#include <array>
#include <cassert>
#include <string_view>
#include "../Sunrise/src/middleware/bap/activity_message/sense_update.h"
namespace sense=sunrise::middleware::bap::activity_message::sense_update;
sense::TargetStatus group(const void*,std::uint32_t key,sense::GroupTarget& out)noexcept{
 if(key!=0xA3B76C64)return sense::TargetStatus::targetUnavailable;
 out.objectTag=0x80B3C21C;return sense::TargetStatus::resolved;
}
sense::TargetStatus slot(const void*,const sense::GroupTarget&,std::uint8_t type,std::uint16_t index,sense::SlotTarget& out)noexcept{
 if(type!=20||index!=121)return sense::TargetStatus::targetUnavailable;
 out.senseSchema=0x80809562;return sense::TargetStatus::resolved;
}
int main(){
 // Native A774B0/80809562: mandatory raw health and shield, signed Auth revision.
 constexpr std::array<std::string_view,3> fixtures{
 "000000000000000000000000000000003476ed8c800000175a3b76c642b00f33f8000000000000080000003000000040",
 "000000000000000000000000000000003476ed8c800000175a3b76c642b00f3000000000000000080000003000000040",
 "000000000000000000000000000000003476ed8c800000175a3b76c642b00f3bf8000000000000080000003000000040",
 };
 const sense::Resolver resolver{nullptr,&group,&slot};
 for(unsigned i=0;i<fixtures.size();++i){
  std::array<std::byte,64> bytes{};auto n=fixtures[i].size()/2;
  auto hex=[](char c){return c<='9'?c-'0':c-'a'+10;};
  for(std::size_t j=0;j<n;++j)bytes[j]=std::byte((hex(fixtures[i][2*j])<<4)|hex(fixtures[i][2*j+1]));
  sense::SenseUpdate out{};std::size_t consumed{};
  assert(sense::decode_sense_update(std::span(bytes).first(n),resolver,out,consumed));
  assert(out.decoded.status==sense::DecodeStatus::complete && out.decoded.objectCount==1);
  const auto& object=out.decoded.objects[0];assert(object.valueCount==3);
  const auto& health=out.decoded.values[object.firstValue];
  const auto& shield=out.decoded.values[object.firstValue+1];
  const auto& revision=out.decoded.values[object.firstValue+2];
  assert(health.present && shield.present);
  assert(health.realValue==(i==0?1.0f:i==1?0.0f:-1.0f)&&shield.realValue==0.0f);
  assert(object.deltaBits==97 && object.status==sense::ObjectStatus::decoded);
  assert(revision.signedValue==3&&object.generationPlusOne==4);
  assert(!sense::decode_sense_update(std::span(bytes).first(n-1),resolver,out,consumed));
 }
}
