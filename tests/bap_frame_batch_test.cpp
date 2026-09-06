#include <cassert>
#include <vector>
#include "../Sunrise/src/server/transport/bap_frame_batch.h"
struct Peer { unsigned streamSize=100, outputSize=0, next=0; };
int main() {
    using sunrise::server::transport::drain_frame_batch;
    Peer p;
    std::vector<unsigned> order;
    bool blocked=false;
    auto drain=[&](Peer& peer) { if(peer.streamSize) { order.push_back(peer.next++); --peer.streamSize; peer.outputSize=2; } return true; };
    auto flush=[&](Peer& peer) { if(!blocked) peer.outputSize=0; return true; };
    assert(drain_frame_batch(p,drain,flush) && p.next==16);
    blocked=true;
    assert(drain_frame_batch(p,drain,flush) && p.next==17 && p.outputSize==2);
    assert(drain_frame_batch(p,drain,flush) && p.next==17);
    blocked=false; flush(p);
    while(p.streamSize) assert(drain_frame_batch(p,drain,flush));
    assert(p.next==100 && order.size()==100);
    for(unsigned i=0;i<order.size();++i) assert(order[i]==i);
    unsigned attempts=0;
    auto partial=[&](Peer&) { ++attempts; return true; };
    p.streamSize=3;
    assert(drain_frame_batch(p,partial,flush) && attempts==1);
    assert(!drain_frame_batch(p,[](Peer&){return false;},flush));
    assert(!drain_frame_batch(p,drain,[](Peer&){return false;}));
}
