#include "ember_movies.h"
#include "playback_rules.h"
#include "resources.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "../../../core/logging/log.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"
namespace sunrise::client::hooks::ember_movies {
namespace {
using namespace patterns;
using Accessor = void*(__fastcall*)();
using Operation = void(__fastcall*)(void*);
using Play = void(__fastcall*)(void*, std::uint32_t, std::uint32_t);
using Busy = bool(__fastcall*)(void*);
struct Api { Accessor manager{}, decoder{}; Operation acquire{}, release{}, stop{}; Play play{}; Busy busy{}; } api;
SRWLOCK lock = SRWLOCK_INIT;
std::atomic_bool watching{false}, frameReady{false};
Owner owner{};
std::uint64_t key{}, began{};
unsigned movie{};
Status state{};
bool acquired{}, stopRequested{}, attempted{}, escapeHeld{};
Playback playback{};
MovieResource resource{};
void* decoderOwner{};
int lastDecoderState{-1};
thread_local bool inPoll{};
constexpr std::array<std::uint32_t, 2> assets{0x80BCA001U, 0x80BCA003U};
void report(const char* result, int decoderState = -1) {
    std::array<char, 240> text{};
    std::snprintf(text.data(), text.size(),
        "ev=ember_movie result=%s movie=%u asset=%08X request=%llu decoder_state=%d",
        result, movie, movie >= 1 && movie <= 2 ? assets[movie-1] : 0,
        static_cast<unsigned long long>(key), decoderState);
    core::log::write(core::log::Channel::client, core::log::Level::info, text.data());
}
void* call_target(std::byte* code, std::size_t offset) {
    if (!code || code[offset] != std::byte{0xE8}) return nullptr;
    std::int32_t relative{}; std::memcpy(&relative, code + offset + 1, 4);
    return code + offset + 5 + relative;
}
bool resolve() {
    if (attempted) return api.play != nullptr;
    attempted = true;
    // Native pre-rendered component start DDB0F0: acquire manager then play config+4C.
    constexpr auto startSig = signature<signature_length("40 56 48 83 EC 20 48 83 79 30 FF 48 8B F1 0F 85 ? ? ? ? E8 ? ? ? ? 84 C0 0F 85")>(
        "40 56 48 83 EC 20 48 83 79 30 FF 48 8B F1 0F 85 ? ? ? ? E8 ? ? ? ? 84 C0 0F 85");
    constexpr auto stopSig = signature<signature_length("40 53 48 83 EC 20 48 83 79 30 FF 48 8B D9 74 22 E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? E8")>(
        "40 53 48 83 EC 20 48 83 79 30 FF 48 8B D9 74 22 E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? E8");
    constexpr auto busySig = signature<signature_length("48 83 EC 28 83 79 58 FF 75 ? 8B 0D ? ? ? ? 33 D2 48 89 5C 24 20 32 DB")>(
        "48 83 EC 28 83 79 58 FF 75 ? 8B 0D ? ? ? ? 33 D2 48 89 5C 24 20 32 DB");
    auto* start = scan_main_image_unique(startSig, "ember_movie_start");
    auto* stop = scan_main_image_unique(stopSig, "ember_movie_stop");
    auto* busy = scan_main_image_unique(busySig, "ember_movie_busy");
    Api candidate{};
    candidate.manager = reinterpret_cast<Accessor>(call_target(start, 0x72));
    candidate.acquire = reinterpret_cast<Operation>(call_target(start, 0x7A));
    candidate.play = reinterpret_cast<Play>(call_target(start, 0x8E));
    candidate.stop = reinterpret_cast<Operation>(call_target(stop, 0x18));
    candidate.release = reinterpret_cast<Operation>(call_target(stop, 0x25));
    candidate.busy = reinterpret_cast<Busy>(busy);
    candidate.decoder = reinterpret_cast<Accessor>(call_target(busy, 0x48));
    if (!candidate.manager || !candidate.acquire || !candidate.play || !candidate.stop
        || !candidate.release || !candidate.busy || !candidate.decoder) { report("signature_failed"); return false; }
    api = candidate; return true;
}
template<class T> T field(void* pointer, unsigned offset) {
    T value{}; std::memcpy(&value, static_cast<std::byte*>(pointer)+offset, sizeof(value)); return value;
}
void release() {
    if (acquired) { api.release(api.manager()); acquired=false; }
}
void fail(const char* reason) {
    // Stop only our exact decoder asset, never another movie's playback.
    if (acquired) {
        auto* decoder=api.decoder();
        if (decoder && decoder==decoderOwner && field<std::uint32_t>(decoder,0x1B4)==assets[movie-1])
            api.stop(api.manager());
        release();
    }
    state=Status::failed; watching.store(!resource.release()); report(reason);
}
}
bool request(Owner next, std::uint64_t nextKey, unsigned index, bool stop) noexcept {
    if (!next.session || !next.generation || !nextKey || index<1 || index>2) return false;
    AcquireSRWLockExclusive(&lock);
    bool accepted=false;
    if (stop) {
        if (next==owner && index==movie && (state==Status::preparing || state==Status::playing)) {
            stopRequested=true; accepted=true;
        }
    } else if (next==owner && nextKey==key && index==movie) {
        accepted=state!=Status::failed;
    } else if (!acquired && !resource.held() && state!=Status::queued && state!=Status::preparing && state!=Status::playing) {
        owner=next; key=nextKey; movie=index; state=Status::queued; began=GetTickCount64();
        stopRequested=false; playback={}; decoderOwner=nullptr; lastDecoderState=-1;
        escapeHeld=(GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0;
        watching.store(true); report("queued"); accepted=true;
    }
    ReleaseSRWLockExclusive(&lock); return accepted;
}
Status status(Owner next, unsigned index) noexcept {
    AcquireSRWLockShared(&lock);
    auto value=next==owner && index==movie ? state : Status::absent;
    ReleaseSRWLockShared(&lock); return value;
}
bool active() noexcept { return watching.load(); }
void frame_ready(bool ready) noexcept { frameReady.store(ready); }
void poll(std::int32_t region, std::int32_t step) noexcept {
    if (inPoll) return;
    inPoll=true;
    struct Reset { ~Reset() { inPoll=false; } } reset;
    AcquireSRWLockExclusive(&lock);
    if (state!=Status::queued && state!=Status::preparing && state!=Status::playing) {
        watching.store(!resource.release());
        ReleaseSRWLockExclusive(&lock); return;
    }
    const auto now=GetTickCount64();
    if (region!=0 || step!=38) { fail("world_changed"); ReleaseSRWLockExclusive(&lock); return; }
    if (!frameReady.load()) { fail("frame_observer_unavailable"); ReleaseSRWLockExclusive(&lock); return; }
    if (!resolve()) { state=Status::failed; watching.store(false); ReleaseSRWLockExclusive(&lock); return; }
    auto* manager=api.manager();
    auto* decoder=api.decoder();
    if (!manager || !decoder) { fail("player_unavailable"); ReleaseSRWLockExclusive(&lock); return; }
    if (state==Status::queued) {
        if (!resource.held() && !resource.begin(assets[movie-1])) {
            fail("resource_request_failed"); ReleaseSRWLockExclusive(&lock); return;
        }
        if (resource.state()==3 || resource.state()<0) {
            fail("resource_load_failed"); ReleaseSRWLockExclusive(&lock); return;
        }
        if (!resource.ready()) {
            if (now-began>30000) fail("resource_ready_timeout");
            ReleaseSRWLockExclusive(&lock); return;
        }
        if (api.busy(manager)) {
            if (now-began>30000) fail("player_busy_timeout");
        } else {
            // Same acquire/play pairing as the authored pre-rendered component.
            report("resource_ready");
            decoderOwner=decoder; api.acquire(manager); acquired=true;
            api.play(manager,assets[movie-1],0); state=Status::preparing; began=now;
            report("submitted");
        }
        ReleaseSRWLockExclusive(&lock); return;
    }
    if (decoder!=decoderOwner) { fail("decoder_owner_changed"); ReleaseSRWLockExclusive(&lock); return; }
    const auto asset=field<std::uint32_t>(decoder,0x1B4);
    const int decoderState=field<int>(decoder,0x1B0);
    if (decoderState!=lastDecoderState) { report("decoder",decoderState); lastDecoderState=decoderState; }
    const auto observed=playback.observe(asset==assets[movie-1],decoderState,api.busy(manager));
    if (observed==Status::playing && state!=Status::playing) report("playing",decoderState);
    // The direct video path has no type-6 source to emit a cinematic-skip incident.
    // A foreground Escape press asks the original native movie player to stop; completion
    // still requires the decoder's subsequent stopped/end receipt for this asset.
    DWORD foregroundProcess{};
    GetWindowThreadProcessId(GetForegroundWindow(),&foregroundProcess);
    const bool escapeDown=(GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0;
    if (observed==Status::playing && foregroundProcess==GetCurrentProcessId()
        && escapeDown && !escapeHeld) stopRequested=true;
    escapeHeld=escapeDown;
    if (stopRequested && asset==assets[movie-1]) {
        api.stop(manager); stopRequested=false; report("stop_requested",decoderState);
    }
    if (observed==Status::complete) { release(); state=observed; watching.store(!resource.release()); report("complete",decoderState); }
    else if (observed==Status::failed) fail("decoder_failed_or_replaced");
    else state=observed;
    if ((state==Status::preparing && now-began>30000)
        || (state==Status::playing && now-began>600000)) fail("playback_timeout");
    ReleaseSRWLockExclusive(&lock);
}
}
