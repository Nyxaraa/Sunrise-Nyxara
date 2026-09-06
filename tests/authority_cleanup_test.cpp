#include <cassert>
#include <algorithm>
#include "../Sunrise/src/middleware/bap/activity_message/authority_cleanup.h"
#include "../Sunrise/src/middleware/encoding/bit_writer.h"
using namespace sunrise::middleware::bap::activity_message;
int main() {
    entity_slots::EntitySlotMask mask{};
    mask[0]=std::byte{1}; mask[127]=std::byte{0x80}; mask[1023]=std::byte{0x80};
    // Independently construct the unaligned client request, including boundary slots.
    for (unsigned reason=1; reason<=8; ++reason) {
        std::array<std::byte,entity_authority::kRequestPurgeByteCount> input{};
        sunrise::middleware::encoding::bits::Writer w(input);
        assert(w.write(reason-1,3));
        for (auto b:mask) assert(w.write(std::to_integer<unsigned>(b),8));
        std::size_t written{};assert(w.finish(written));
        host_control::PurgeAuthorityBody result{};
        assert(authority_cleanup::prepare(27,input,254,result));
        assert(result.slots==mask && result.epoch==255 && static_cast<unsigned>(result.reason)==reason);
        std::array<std::byte,host_control::kPurgeAuthorityByteCount> output{};
        assert(host_control::encode_purge_authority(result,output,written));
        host_control::PurgeAuthorityBody decoded{};
        assert(host_control::decode_purge_authority(output,decoded));
        assert(decoded.slots==mask && decoded.epoch==255 && static_cast<unsigned>(decoded.reason)==reason);
        assert(!authority_cleanup::prepare(27,std::span(input).first(input.size()-1),0,result));
        input.back() |= std::byte{1};
        assert(!authority_cleanup::prepare(27,input,0,result));
    }
    std::array<std::byte,entity_authority::kAbandonByteCount> abandoned{};
    abandoned[0]=std::byte{7};
    std::copy(mask.begin(),mask.end(),abandoned.begin()+1);
    abandoned.back()=std::byte{0x60}; // reason 4
    host_control::PurgeAuthorityBody result{};
    assert(authority_cleanup::prepare(26,abandoned,2,result));
    assert(result.slots==mask && result.reason==4 && result.epoch==3);
    for (unsigned current = 0; current < 255; ++current) {
        assert(authority_cleanup::prepare(26,abandoned,static_cast<std::uint8_t>(current),result));
        assert(result.epoch == current + 1);
    }
    assert(!authority_cleanup::prepare(26,abandoned,255,result));
    assert(result.epoch == 255);
    assert(!authority_cleanup::prepare(33,std::span(abandoned).first(1025),2,result));
    abandoned[0]=std::byte{65};assert(!authority_cleanup::prepare(26,abandoned,2,result));
    abandoned.fill(std::byte{});assert(!authority_cleanup::prepare(26,abandoned,2,result));
    assert(result.slots==mask && result.reason==4); // refused input never changes output
}
