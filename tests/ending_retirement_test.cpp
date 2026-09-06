#include <array>
#include <cassert>
#include "../Sunrise/src/client/hooks/mission_retirement/mission_retirement.h"
#include "../Sunrise/src/middleware/bap/activity_message/roster_presence.h"
#include "../Sunrise/src/middleware/encoding/bit_reader.h"
#include "../Sunrise/src/state/activity/membership/teleport_rules.h"
#include "../Sunrise/src/core/logging/log.h"
namespace sunrise::core::log {
bool accepts(Channel, Level) noexcept { return false; }
void write(Channel, Level, std::string_view) noexcept {}
}
namespace wire = sunrise::middleware::bap::activity_message::sensor_auth_update;
namespace retire = sunrise::client::hooks::mission_retirement;
namespace member = sunrise::state::activity::membership;
using sunrise::middleware::encoding::bits::Reader;
static void expect(Reader& reader, unsigned width, std::uint64_t expected) {
    std::uint64_t value{};
    assert(reader.read(static_cast<std::uint8_t>(width), value) && value == expected);
}
int main() {
    std::array<std::uint32_t, 5> history{102, 103, 104};
    std::size_t historyCount = 3;
    const std::array<std::uint32_t, 3> movieOne{105, 102, 104};
    const std::array<std::uint32_t, 3> movieTwo{106, 105, 104};
    assert(wire::extend_key_order(history, historyCount, movieOne));
    assert(wire::extend_key_order(history, historyCount, movieTwo));
    assert(historyCount == 5 && (history == std::array<std::uint32_t, 5>{102,103,104,105,106}));
    assert(wire::extend_key_order(history, historyCount, movieOne));
    const std::array<std::uint32_t, 1> overflow{107};
    assert(!wire::extend_key_order(history, historyCount, overflow));
    // Unknown/empty worlds and a different owner cannot masquerade as cleanup.
    retire::Progress progress{retire::Status::baselinePending, 0};
    progress.observe(0, 1234, 4, 0);
    assert(progress.value == retire::Status::baselinePending);
    progress.observe(0, 1234, 8, 4);
    assert(progress.value == retire::Status::retiring);
    progress.observe(1, 1234, 4, 0);
    progress.observe(0, 5678, 4, 0);
    progress.observe(0, 1234, 0, 0);
    progress.observe(0, 1234, 5, 1);
    assert(progress.value == retire::Status::retiring);
    progress.observe(0, 1234, 4, 0);
    assert(progress.value == retire::Status::complete);

    // Exercise the actual encoder: old keys retain their ordinal, a clear bit removes
    // only the retired key, and phase two contains no body for that removed group.
    static wire::Snapshot snapshot{};
    std::array<std::uint8_t, 1> types{1}, flags{0};
    std::array<std::uint16_t, 1> indices{0};
    std::array<std::uint32_t, 3> keys{102, 103, 104};
    std::array<wire::BubbleSubBlock, 1> blocks{{{0, keys}}};
    snapshot.roster.groupCount = 4; snapshot.roster.topLevelGroupCount = 1;
    snapshot.roster.bubbleSubBlocks = blocks;
    for (unsigned i = 0; i < 4; ++i) {
        auto& group = snapshot.roster.groups[i];
        group.key = 101 + i; group.slotTypes = types; group.slotFlags = flags; group.slotIndices = indices;
    }
    snapshot.roster.groups[2].retired = true;
    std::array<std::byte, 4096> bytes{}; std::size_t size{};
    assert(wire::encode_sensor_auth_update(snapshot, bytes, size));
    Reader mask{std::span(bytes).first(size)};
    assert(mask.skip(wire::kLatchBitWithoutGrant + 1 + wire::delta_bits(1, {}) - 1));
    expect(mask, 1, 1); expect(mask, 7, 1); // Field one, one bubble.
    expect(mask, 1, 1); expect(mask, 32, 0x80000000U);
    expect(mask, 1, 1); expect(mask, 1, 1); expect(mask, 7, 3);
    for (auto key : keys) expect(mask, 32, key);
    expect(mask, 1, 1); expect(mask, 32, 5); // Keep ordinals 0 and 2.
    expect(mask, 32, 0); expect(mask, 32, 0);
    Reader bodies{std::span(bytes).first(size)};
    assert(bodies.skip(wire::kLatchBitWithoutGrant + 1 + wire::delta_bits(1, blocks)));
    for (auto key : {101U, 102U, 104U}) {
        expect(bodies, 1, 1); expect(bodies, 32, key); expect(bodies, 32, 0);
        expect(bodies, 1, 1); expect(bodies, 32, key); expect(bodies, 7, 2);
        expect(bodies, 16, 32768); expect(bodies, 32, 0); expect(bodies, 1, 0);
    }
    expect(bodies, 1, 0); expect(bodies, 1, 0);
    snapshot.roster.groups[0].retired = true;
    assert(!wire::encode_sensor_auth_update(snapshot, bytes, size));

    member::MembershipState state{};
    state.hasHostTeleport = true; state.hostTeleportQualified = true;
    state.hostTeleport = {1, 8, 1, 123}; state.teleport = {2, 8, 1, 123};
    state.region.index = 1; state.currentReported = true; state.currentRegion.index = 0;
    member::observe_qualified_teleport(state);
    assert(state.hostTeleport.state == 1); // Pending-region advertisement is not arrival.
    state.teleport.state = 3; state.currentRegion.index = 1;
    state.teleport.sliceSetHash = 124;
    member::observe_qualified_teleport(state); assert(state.hostTeleport.state == 1);
    state.teleport.sliceSetHash = 123; state.teleport.token = 7;
    member::observe_qualified_teleport(state); assert(state.hostTeleport.state == 1);
    state.teleport.token = 8;
    member::observe_qualified_teleport(state); assert(state.hostTeleport.state == 3);
    state.teleport.state = 0;
    member::observe_qualified_teleport(state); assert(!state.hasHostTeleport);
    assert(member::next_teleport_token(255) == 1 && member::next_teleport_token(8) == 9);
}
