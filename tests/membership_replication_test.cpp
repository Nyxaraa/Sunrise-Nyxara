#include <array>
#include <cassert>
#include <cstdint>
#include "../Sunrise/src/middleware/bap/activity_message/replicate_membership.h"
#include "../Sunrise/src/middleware/encoding/bit_reader.h"

namespace membership = sunrise::middleware::bap::activity_message::replicate_membership;
using sunrise::middleware::encoding::bits::Reader;

// Read the native top-level fields after the member/region body. Field 6 must
// be explicitly present: absence preserves the previous disabled send gate.
static void check(const membership::MembershipSnapshot& snapshot,
                  std::uint32_t occupied, std::uint32_t transmitting) {
    std::array<std::byte, 8192> bytes{};
    std::size_t size{};
    assert(membership::encode_replicate_membership(snapshot, bytes, size));
    assert(size == membership::encoded_size(snapshot));
    Reader reader{std::span(bytes).first(size)};
    assert(reader.skip(membership::region_block_end_bit(snapshot)));
    std::uint64_t value{};
    for (const auto expected : {occupied, occupied, transmitting}) {
        assert(reader.read(1, value) && value == 1);
        assert(reader.read(32, value) && value == expected);
    }
    assert(reader.read(1, value) && value == 0); // field 7
    assert(reader.read(1, value) && value == 0); // field 8
    assert(reader.remaining_bits() < 8);
    while (reader.remaining_bits()) assert(reader.read(1, value) && value == 0);
    size = 99;
    assert(!membership::encode_replicate_membership(
        snapshot, std::span(bytes).first(membership::encoded_size(snapshot) - 1), size));
    assert(size == 0);
}

int main() {
    membership::MembershipSnapshot snapshot{};
    check(snapshot, 1, 0);
    auto& remote = snapshot.remoteViewMember;
    remote.present = true;
    remote.identity.memberKey = 2;
    remote.identity.field1 = 0;
    remote.identity.field3 = 3;
    remote.identity.accountSoid = 4;
    remote.identity.field5 = 5;
    remote.identity.field6 = 6;
    remote.address[0] = std::byte{1};
    remote.processSessionIdHash = 0xCBF29CE484222325ULL; // FNV of empty terminated string
    check(snapshot, 3, 2);
    snapshot.selfHosted = true;
    snapshot.selfHostedRegion = 64;
    snapshot.currentLeg.present = true;
    snapshot.currentLeg.sliceSetIndex = 8;
    snapshot.currentLeg.regionIndex = 64;
    snapshot.pendingLeg = snapshot.currentLeg;
    snapshot.pendingLeg.regionIndex = 72;
    check(snapshot, 3, 2);
    snapshot.citizenCount = 1;
    snapshot.citizens[0].present = true;
    snapshot.citizens[0].regionIndex = 72;
    snapshot.citizens[0].ambassadorSlot = 1;
    check(snapshot, 3, 2);
    snapshot.remoteViewMember.present = false;
    check(snapshot, 1, 0); // Explicitly clear a previously published remote gate.
}
