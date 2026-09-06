#include <array>
#include <cassert>
#include "../Sunrise/src/client/hooks/ember_movies/readiness_rules.h"
#include "../Sunrise/src/client/hooks/ember_movies/sunburn_rules.h"
#include "../Sunrise/src/client/hooks/ember_movies/playback_rules.h"
#include "../Sunrise/src/client/hooks/ember_movies/surface_rules.h"
#include "../Sunrise/src/client/hooks/ember_movies/orbit_rules.h"
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
    namespace movies=sunrise::client::hooks::ember_movies;
    movies::OrbitReturn orbit;
    movies::OrbitObservation inApex{true,true,false,false,true,0,38,38,true};
    using OA=movies::OrbitAction;
    assert(orbit.observe(1000,inApex)==OA::none); // Playing movies cannot arm return.
    orbit.arm(1000);
    assert(orbit.observe(9000,inApex)==OA::none); // Host must actually publish completion.
    inApex.complete=true;
    assert(orbit.observe(10000,inApex)==OA::select); // First completion frame: no banner delay.
    assert(orbit.observe(10001,inApex)==OA::none); // No duplicate commit.
    auto inOrbit=inApex; inOrbit.emberSelected=false;inOrbit.orbitSelected=true;
    inOrbit.exactOwner=false; // Committing orbit may already retire the old ActivityClient link.
    assert(orbit.observe(10002,inOrbit)==OA::cleanup); // Requires destination readback.
    assert(orbit.observe(10003,inOrbit)==OA::none); // No repeated cleanup request.
    movies::OrbitObservation betweenWorlds{};betweenWorlds.step=28;
    assert(orbit.observe(10004,betweenWorlds)==OA::none);
    assert(orbit.active());
    inOrbit.step=29;
    assert(orbit.observe(10100,inOrbit)==OA::orbitSetup);
    assert(!orbit.active() && orbit.observe(20000,inApex)==OA::none);
    orbit.arm(30000);
    auto stale=inApex;stale.exactOwner=false;
    assert(orbit.observe(31000,stale)==OA::canceled);
    orbit.arm(40000);
    auto departed=inApex;departed.step=28;
    assert(orbit.observe(41000,departed)==OA::canceled);
    orbit.arm(50000);inApex.complete=false;
    assert(orbit.observe(140001,inApex)==OA::timedOut);
    orbit.arm(150000);inApex.complete=true;
    inApex.ready=false;
    assert(orbit.observe(150000,inApex)==OA::none);
    inApex.ready=true;
    assert(orbit.observe(150001,inApex)==OA::select); // No delay once native readiness returns.
    assert(orbit.observe(150002,departed)==OA::none); // Native cleanup already began.
    assert(orbit.observe(150100,inOrbit)==OA::orbitSetup);
    orbit.arm(160000);
    inApex.pendingStep=28;
    assert(orbit.observe(160000,inApex)==OA::none);
    inApex.pendingStep=38;
    assert(orbit.observe(160001,inApex)==OA::select);
    auto replaced=inOrbit;replaced.step=38;replaced.sameWorld=false;
    assert(orbit.observe(160002,replaced)==OA::canceled);
    // a47adbd: container 80BCA022 loaded, but definition 80BCA021 was
    // FEFE0000 / 001044FB / null. Native 1204163 faulted reading its slot.
    assert(!movies::movie_definition_resident(0xFEFE0000,0x1044FB,0));
    assert(!movies::movie_definition_resident(0xFEFE0000,0x1044FB,0x1234000));
    assert(!movies::movie_definition_resident(0xC0000010,0x44FB,0));
    assert(!movies::movie_definition_resident(0xC0000008,0x44FB,0x1234000));
    assert(!movies::movie_definition_resident(0xC0000010,0x254FB,0x1234000)); // raw buffer is not a definition
    assert(movies::movie_definition_resident(0xC0000010,0x44FB,0x1234000));
    assert(movies::movie_definition_resident(0xC0000010,0x4044FB,0x1234000)); // native residency flags
    for (unsigned i=0;i<6;++i) {
        assert(movies::movie_definition_matches(i,i+1,i<2 ? 0x7F : 0x23));
        assert(!movies::movie_definition_matches(i,0,i<2 ? 0x7F : 0x23));
    }
    assert(!movies::movie_definition_matches(0,1,0x23));
    assert(!movies::movie_definition_matches(6,7,0x23));
    movies::SurfaceRegistrations surfaces{};
    for (unsigned i=1;i<=6;++i) surfaces[i].entries[0]=movies::movie_surface_definitions[i-1];
    // Live black-video capture: old candidate handles remain, but no container
    // holds a registration and every selected surface is FFFFFFFF.
    assert(!movies::movie_surfaces_registered(surfaces));
    assert(movies::movie_surfaces_absent(surfaces)); // stale candidates do not retain references
    for (unsigned i=1;i<=6;++i) surfaces[i].count=1;
    assert(!movies::movie_surfaces_absent(surfaces)); // hold definitions until native unregister
    assert(movies::movie_surfaces_registered(surfaces));
    assert(!movies::movie_surfaces_selected(surfaces));
    for (unsigned i=1;i<=6;++i) surfaces[i].selected=surfaces[i].entries[0];
    assert(movies::movie_surfaces_selected(surfaces));
    surfaces[4].count=0;
    assert(!movies::movie_surfaces_registered(surfaces));
    surfaces[4].count=4;
    assert(!movies::movie_surfaces_registered(surfaces));
    surfaces[4].count=2;surfaces[4].entries[1]=123;
    assert(!movies::movie_surfaces_registered(surfaces)); // another surface owns the top
    surfaces[4].count=1;
    surfaces[7].count=1;surfaces[7].entries[0]=123;
    assert(!movies::movie_surfaces_registered(surfaces)); // do not publish an unrelated pending slot
    surfaces[7].selected=123;
    assert(movies::movie_surfaces_selected(surfaces));
    auto removed=surfaces;
    for (unsigned i=1;i<=6;++i) removed[i].count=0;
    assert(!movies::movie_surfaces_absent(removed)); // selection is still using the definition
    for (unsigned i=1;i<=6;++i) removed[i].selected=0xFFFFFFFF;
    assert(movies::movie_surfaces_absent(removed));
    // Native kind 2 resolves shared-tag records; these ordinary movie tags require 1.
    static_assert(movies::movie_resource_kind==1);
    assert((movies::movie_metadata(0x80BCA001)==std::array<std::uint32_t,4>{0x80BCA001,0x80BCA000,0x80B9EB33,0x80BCA032}));
    assert((movies::movie_metadata(0x80BCA003)==std::array<std::uint32_t,4>{0x80BCA003,0x80BCA002,0x80B9EB34,0x80BCA032}));
    assert((movies::movie_metadata(0x80BCA034)==std::array<std::uint32_t,4>{})); // media is not a wrapper
    assert(movies::movie_stream(0x80BCA001)==0x80BCA034);
    assert(movies::movie_stream(0x80BCA003)==0x80C7C000);
    assert(movies::movie_stream(0x80BCA000)==0xFFFFFFFF);
    // The 13f07ee capture: metadata was loaded, but the video stream still held a
    // free-list entry (FEFE0035, 0, location 0). Native CRI entered error state 7.
    assert(!movies::movie_stream_ready(0xFEFE0035,0,0));
    assert(movies::movie_stream_ready(0xE80779A0,0x41363B,0x10000005));
    assert(!movies::movie_stream_ready(0xC0000000,0x41363B,0x10000005));
    assert(!movies::movie_stream_ready(0x280779A0,0x41363B,0x10000005));
    assert(!movies::movie_stream_ready(0xE80779A0,0x40103B,0x10000005));
    assert(!movies::movie_stream_ready(0xE80779A0,0x41363B,0));
    assert(!movies::movie_stream_ready(0xE80779A0,0x41363B,0x100000005ULL));
    // Captured crash: registered movie tags contain free-list entries, not resident headers.
    assert(!movies::movie_resources_ready(1,0x80BCA001,false,0,false,0xFFFFFFFF));
    assert(!movies::movie_resources_ready(2,0x80BCA001,false,0x80BCA000,true,0x80BCA034));
    assert(!movies::movie_resources_ready(2,0x80BCA001,true,0x80BCA000,false,0x80BCA034));
    assert(!movies::movie_resources_ready(2,0x80BCA001,true,0x80BCA002,true,0x80BCA034));
    assert(!movies::movie_resources_ready(3,0x80BCA001,true,0x80BCA000,true,0x80BCA034));
    assert(!movies::movie_resources_ready(2,0x80BCA001,true,0x80BCA000,true,0xFFFFFFFF));
    assert(movies::movie_resources_ready(2,0x80BCA001,true,0x80BCA000,true,0x80BCA034));
    assert(movies::movie_resources_ready(2,0x80BCA003,true,0x80BCA002,true,0x80C7C000));
    assert(!movies::resource_can_release(0) && !movies::resource_can_release(1));
    assert(movies::resource_can_release(2) && movies::resource_can_release(3));
    // Only Ember slot 43 may borrow sunburn. The global Foundry effect and rail shock stay distinct.
    assert(movies::ember_burn_source(0x80B3C0C6,0x80809540,0xAC8,0x80C1D9E0));
    assert(!movies::ember_burn_source(0x80BEB26F,0x80809540,0xAC8,0x80C1D9E0));
    assert(!movies::ember_burn_source(0x80B3C0C6,0x8080953F,0xAC8,0x80C1D9E0));
    assert(!movies::ember_burn_source(0x80B3C0C6,0x80809540,0xAC0,0x80C1D9E0));
    assert(!movies::ember_burn_source(0x80B3C0C6,0x80809540,0xAC8,0x80C1D389));
    movies::Playback playback;
    assert(playback.observe(false,5,true)==movies::Status::preparing);
    assert(playback.observe(true,0,false)==movies::Status::preparing);
    assert(playback.observe(true,6,false)==movies::Status::preparing);
    assert(playback.observe(true,3,true)==movies::Status::preparing);
    assert(playback.observe(true,5,true)==movies::Status::playing);
    assert(playback.observe(true,6,true)==movies::Status::playing);
    assert(playback.observe(true,6,false)==movies::Status::complete);
    assert(playback.observe(false,6,false)==movies::Status::failed);
    assert(playback.observe(true,7,false)==movies::Status::failed);
    movies::Playback skipped;
    assert(skipped.observe(true,5,true)==movies::Status::playing);
    assert(skipped.observe(true,0,false)==movies::Status::complete);

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
