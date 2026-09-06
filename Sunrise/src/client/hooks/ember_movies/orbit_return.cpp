#include "orbit_return.h"
#include "orbit_rules.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"
#include "../../../core/logging/log.h"
#include "../../../server/bap/runtime.h"
#include "../../../state/activity/runtime.h"

namespace sunrise::client::hooks::ember_movies::orbit_return {
namespace {
using namespace patterns;
using Accessor=void*(__fastcall*)();
using Construct=void*(__fastcall*)(void*);
using Clear=void(__fastcall*)();
using Select=void(__fastcall*)(int,void*);
using Commit=void(__fastcall*)(int);
using Predicate=bool(__fastcall*)(int);
using Activity=void*(__fastcall*)(std::uint16_t*);
using Lifetime=int(__fastcall*)();
using Step=void(__fastcall*)(void*,int,int);
struct Api { Accessor manager{}; Construct construct{}, orbit{}; Clear clear{};
    Select select{}; Commit commit{}; Predicate mode{}; Activity activity{};
    Lifetime lifetime{}; Step step{}; } api;
OrbitReturn progress{};
Owner owner{};
std::uint64_t arrival{};
std::atomic_bool watching{};
bool attempted{}, available{};
thread_local bool inPoll{};
template<class T> T field(const void* pointer,unsigned offset) {
    T value{};std::memcpy(&value,static_cast<const std::byte*>(pointer)+offset,sizeof(value));return value;
}
void* call(std::byte* code,unsigned offset) {
    if (!code || code[offset]!=std::byte{0xE8}) return nullptr;
    return code+offset+5+field<std::int32_t>(code,offset+1);
}
void report(const char* result) {
    std::array<char,192> line{};
    std::snprintf(line.data(),line.size(),"ev=ember_orbit result=%s session=%llu generation=%llu",
        result,static_cast<unsigned long long>(owner.session),static_cast<unsigned long long>(owner.generation));
    core::log::write(core::log::Channel::client,core::log::Level::info,line.data());
}
bool resolve() {
    if (attempted) return available;
    attempted=true;
    // E19D50's in-world return path uses these exact constructor/clear/select/commit calls.
    constexpr auto returnSig=signature<signature_length("48 81 EC 58 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 40 01 00 00 B9 03 00 00 00 E8 ? ? ? ? 84 C0 74 7E")>(
        "48 81 EC 58 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 40 01 00 00 B9 03 00 00 00 E8 ? ? ? ? 84 C0 74 7E");
    constexpr auto activitySig=signature<signature_length("48 89 5C 24 18 55 48 8D AC 24 F0 FD FF FF 48 81 EC 10 03 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 00 02 00 00 48 8B D9 E8 ? ? ? ? 4C 8B C0 33 D2 8B C2")>(
        "48 89 5C 24 18 55 48 8D AC 24 F0 FD FF FF 48 81 EC 10 03 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 00 02 00 00 48 8B D9 E8 ? ? ? ? 4C 8B C0 33 D2 8B C2");
    constexpr auto lifetimeSig=signature<signature_length("48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 0F 0F B6 00 3C FF 74 08 0F BE C0 48 83 C4 28 C3 83 C8 FF 48 83 C4 28 C3")>(
        "48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 0F 0F B6 00 3C FF 74 08 0F BE C0 48 83 C4 28 C3 83 C8 FF 48 83 C4 28 C3");
    constexpr auto stepSig=signature<signature_length("48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 D0 BC FF FF B8 30 44 00 00 E8 ? ? ? ? 48 2B E0")>(
        "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 D0 BC FF FF B8 30 44 00 00 E8 ? ? ? ? 48 2B E0");
    auto* native=scan_main_image_unique(returnSig,"ember_orbit_return");
    api.mode=reinterpret_cast<Predicate>(call(native,0x1E));
    api.manager=reinterpret_cast<Accessor>(call(native,0x2F));
    api.construct=reinterpret_cast<Construct>(call(native,0x5B));
    api.orbit=reinterpret_cast<Construct>(call(native,0x65));
    api.clear=reinterpret_cast<Clear>(call(native,0x6A));
    api.select=reinterpret_cast<Select>(call(native,0x7E));
    api.commit=reinterpret_cast<Commit>(call(native,0x86));
    api.activity=reinterpret_cast<Activity>(scan_main_image_unique(activitySig,"ember_orbit_activity"));
    api.lifetime=reinterpret_cast<Lifetime>(scan_main_image_unique(lifetimeSig,"ember_orbit_lifetime"));
    api.step=reinterpret_cast<Step>(scan_main_image_unique(stepSig,"ember_orbit_cleanup"));
    available=api.mode && api.manager && api.construct && api.orbit && api.clear && api.select
        && api.commit && api.activity && api.lifetime && api.step;
    return available;
}
bool select_orbit() {
    // Native selection is 0x118 bytes plus the constructor's ownership byte at +118.
    alignas(16) std::array<std::byte,0x120> selection{};
    api.construct(selection.data());
    api.orbit(selection.data());
    // Refuse a missing catalog/default or a non-orbit default before changing selection.
    if (field<std::uint8_t>(selection.data(),0)!=2 || field<std::uint16_t>(selection.data(),2)!=0)
        return false;
    api.clear();
    api.select(0,selection.data());
    api.commit(2);
    return true;
}
}
void arm(Owner next) noexcept {
    owner=next; arrival=state::activity::world_arrival_revision();
    progress.arm(GetTickCount64());watching.store(true);report("awaiting_completion");
}
bool active() noexcept { return watching.load(); }
void poll(int region,int step) noexcept {
    if (!watching.load() || inPoll) return;
    inPoll=true;
    struct Reset { ~Reset(){inPoll=false;} } reset;
    if (!resolve()) { watching.store(false);report("signature_failed");return; }
    // This runs after the movie lock is released; never take the BAP lock from a movie request.
    server::bap::CurrentActivityLinkView link{};
    const bool sameWorld=region==0 && arrival==state::activity::world_arrival_revision();
    const bool exact=server::bap::current_activity_link_view(region,link)
        && link.binding.sessionId==owner.session && link.activityClientGeneration==owner.generation
        && sameWorld;
    std::uint16_t selected=0xFFFF;
    auto* manager=api.manager();
    const bool ready=manager && api.mode(3);
    if (ready) api.activity(&selected);
    const bool ember=exact && selected==link.binding.destination.activityIndex;
    const bool complete=exact && api.lifetime()==6;
    const auto action=progress.observe(GetTickCount64(),{exact,ember,selected==0,complete,ready,
        region,step,manager?field<int>(manager,0x3A0):-1,sameWorld});
    switch(action) {
    case OrbitAction::select:
        report("completion_accepted");
        if (select_orbit()) report("selection_queued");
        else { progress.stage=OrbitStage::finished;report("orbit_selection_unavailable"); }
        break;
    case OrbitAction::cleanup:
        // The destination must already read back as orbit. Use native deferred cleanup;
        // it retires the world on its own tick, outside the video decoder callback.
        api.step(manager,28,309);report("cleanup_requested");break;
    case OrbitAction::orbitSetup: report("orbit_setup");break;
    case OrbitAction::canceled: report("canceled_world_or_owner_changed");break;
    case OrbitAction::timedOut: report("transition_timeout");break;
    default: break;
    }
    watching.store(progress.active());
}
}
