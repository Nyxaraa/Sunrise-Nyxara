#include <cassert>
#include "../Sunrise/src/server/gameplay/peer/peer_external_ack.h"
namespace peer = sunrise::server::gameplay::peer;
namespace wire = sunrise::middleware::gameplay::peer;
int main() {
    sunrise::state::gameplay::PeerLink link{};
    link.viewGeneration=7;
    peer::DisplacedExternals completed{};
    wire::AckState ack{};
    // More than four complete packet-ring wraps must remain available even when
    // the incoming gameplay payload is unsupported; only header ACKs are needed.
    for (unsigned i=0;i<600;++i) {
        auto& slot=link.externalContributions[i%128];
        assert(!slot.occupied);
        slot={};slot.occupied=true;slot.groupSessionId=91;slot.transmissionId=i+1;
        slot.packetSequence=i%1024;slot.viewGeneration=7;slot.commonPresent=true;
        ack.ringInitialized=true;ack.receiveHead=i%128;
        assert(peer::acknowledge_external(link,ack,completed)==1);
        assert(!slot.occupied && completed[0].transmissionId==i+1);
        assert(completed[0].outcome==wire::AckOutcome::received);
    }
    assert(link.commonCommitted);
    link.commonCommitted=false;
    auto& slot=link.externalContributions[0];
    slot.occupied=true;slot.packetSequence=5;slot.viewGeneration=6;slot.commonPresent=true;
    ack.receiveHead=4;
    assert(peer::acknowledge_external(link,ack,completed)==0 && slot.occupied);
    ack.receiveHead=5;
    assert(peer::acknowledge_external(link,ack,completed)==1 && !slot.occupied);
    assert(!link.commonCommitted); // an old view ACK cannot confirm the new common root
    assert(peer::acknowledge_external(link,ack,completed)==0); // duplicate ACK
}
