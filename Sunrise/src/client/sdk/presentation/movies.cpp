#include "movies.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "../../../core/logging/log.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"
#include "orbit_return.h"
#include "playback_rules.h"
#include "resources.h"
namespace sunrise::client::sdk::presentation {
namespace {
using namespace patterns;
using Accessor = void*(__fastcall*)();
using Operation = void(__fastcall*)(void*);
using Play = void(__fastcall*)(void*, std::uint32_t, std::uint32_t);
using Busy = bool(__fastcall*)(void*);
struct Api {
    Accessor manager{}, decoder{};
    Operation acquire{}, release{}, stop{};
    Play play{};
    Busy busy{};
} api;
SRWLOCK lock = SRWLOCK_INIT;
std::atomic_bool watching{false}, frameReady{false};
std::atomic_bool presentation{false}, uiReady{false};
Owner owner{};
Owner firstCompleted{};
std::uint64_t key{}, began{};
unsigned movie{};
Status state{};
bool acquired{}, stopRequested{}, attempted{}, escapeHeld{};
Playback playback{};
MovieResource resource{};
void* decoderOwner{};
int lastDecoderState{-1};
thread_local bool inPoll{};
Config config{};
std::int32_t heldRegion{-1};
std::uint64_t arrival{};
bool continueSequence{};
void report(const char* result, int decoderState = -1) {
    std::array<char, 240> text{};
    std::snprintf(text.data(),
                  text.size(),
                  "ev=movie result=%s movie=%u asset=%08X request=%llu decoder_state=%d",
                  result,
                  movie,
                  movie >= 1 && movie <= config.movieCount ? config.movies[movie - 1].asset : 0,
                  static_cast<unsigned long long>(key),
                  decoderState);
    core::log::write(core::log::Channel::client, core::log::Level::info, text.data());
}
void* call_target(std::byte* code, std::size_t offset) {
    if (!code || code[offset] != std::byte{0xE8}) return nullptr;
    std::int32_t relative{};
    std::memcpy(&relative, code + offset + 1, 4);
    return code + offset + 5 + relative;
}
bool resolve() {
    if (attempted) return api.play != nullptr;
    attempted = true;
    // Native pre-rendered component start DDB0F0: acquire manager then play config+4C.
    constexpr auto startSig = signature<signature_length(
        "40 56 48 83 EC 20 48 83 79 30 FF 48 8B F1 0F 85 ? ? ? ? E8 ? ? ? ? 84 C0 0F 85")>(
        "40 56 48 83 EC 20 48 83 79 30 FF 48 8B F1 0F 85 ? ? ? ? E8 ? ? ? ? 84 C0 0F 85");
    constexpr auto stopSig = signature<signature_length(
        "40 53 48 83 EC 20 48 83 79 30 FF 48 8B D9 74 22 E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? E8")>(
        "40 53 48 83 EC 20 48 83 79 30 FF 48 8B D9 74 22 E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? E8");
    constexpr auto busySig = signature<signature_length(
        "48 83 EC 28 83 79 58 FF 75 ? 8B 0D ? ? ? ? 33 D2 48 89 5C 24 20 32 DB")>(
        "48 83 EC 28 83 79 58 FF 75 ? 8B 0D ? ? ? ? 33 D2 48 89 5C 24 20 32 DB");
    auto* start = scan_main_image_unique(startSig, "movie_start");
    auto* stop = scan_main_image_unique(stopSig, "movie_stop");
    auto* busy = scan_main_image_unique(busySig, "movie_busy");
    Api candidate{};
    candidate.manager = reinterpret_cast<Accessor>(call_target(start, 0x72));
    candidate.acquire = reinterpret_cast<Operation>(call_target(start, 0x7A));
    candidate.play = reinterpret_cast<Play>(call_target(start, 0x8E));
    candidate.stop = reinterpret_cast<Operation>(call_target(stop, 0x18));
    candidate.release = reinterpret_cast<Operation>(call_target(stop, 0x25));
    candidate.busy = reinterpret_cast<Busy>(busy);
    candidate.decoder = reinterpret_cast<Accessor>(call_target(busy, 0x48));
    if (!candidate.manager || !candidate.acquire || !candidate.play || !candidate.stop
        || !candidate.release || !candidate.busy || !candidate.decoder) {
        report("signature_failed");
        return false;
    }
    api = candidate;
    return true;
}
template <class T> T field(void* pointer, unsigned offset) {
    T value{};
    std::memcpy(&value, static_cast<std::byte*>(pointer) + offset, sizeof(value));
    return value;
}
void release() {
    if (acquired) {
        api.release(api.manager());
        acquired = false;
    }
}
void fail(const char* reason) {
    presentation.store(false);
    // Stop only our exact decoder asset, never another movie's playback.
    if (acquired) {
        auto* decoder = api.decoder();
        if (decoder && decoder == decoderOwner
            && field<std::uint32_t>(decoder, 0x1B4) == config.movies[movie - 1].asset)
            api.stop(api.manager());
        release();
    }
    state = Status::failed;
    watching.store(!resource.release());
    report(reason);
}
} // namespace
bool request(Owner next,
             std::uint64_t nextKey,
             unsigned index,
             bool stop,
             bool keepPresentation,
             std::int32_t region) noexcept {
    const auto registration = find(next);
    if (!registration || !nextKey || index < 1 || index > registration->config.movieCount
        || region < 0 || (keepPresentation && index == registration->config.movieCount))
        return false;
    AcquireSRWLockExclusive(&lock);
    bool accepted = false;
    if (stop) {
        if (next == owner && index == movie
            && (state == Status::preparing || state == Status::playing)) {
            stopRequested = true;
            accepted = true;
        }
    } else if (next == owner && nextKey == key && index == movie) {
        accepted = state != Status::failed;
    } else if (!acquired
               && (!resource.held()
                   || (continueSequence
                       && can_chain_movies(owner, next, firstCompleted, movie, index, state)))
               && state != Status::queued && state != Status::preparing
               && state != Status::playing) {
        if (index == 1 || !(next == owner)) firstCompleted = {};
        config = registration->config;
        heldRegion = region;
        arrival = world_revision();
        continueSequence = keepPresentation;
        owner = next;
        key = nextKey;
        movie = index;
        state = Status::queued;
        began = GetTickCount64();
        stopRequested = false;
        playback = {};
        decoderOwner = nullptr;
        lastDecoderState = -1;
        escapeHeld = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        watching.store(true);
        report("queued");
        accepted = true;
    }
    ReleaseSRWLockExclusive(&lock);
    return accepted;
}
Status status(Owner next, unsigned index) noexcept {
    AcquireSRWLockShared(&lock);
    auto value = next == owner && index == movie ? state : Status::absent;
    ReleaseSRWLockShared(&lock);
    return value;
}
bool active() noexcept {
    return watching.load() || orbit_return::active();
}
bool presenting() noexcept {
    return presentation.load();
}
void ui_ready(bool ready) noexcept {
    uiReady.store(ready);
}
void frame_ready(bool ready) noexcept {
    frameReady.store(ready);
}
void poll(std::int32_t region, std::int32_t step) noexcept {
    if (inPoll) return;
    inPoll = true;
    struct Reset {
        ~Reset() {
            inPoll = false;
        }
    } reset;
    AcquireSRWLockExclusive(&lock);
    if (state != Status::queued && state != Status::preparing && state != Status::playing) {
        // Retain shared surfaces only across a bounded, explicitly requested handoff.
        const bool betweenMovies = state == Status::complete && continueSequence
                                   && region == heldRegion && step == 38
                                   && arrival == world_revision()
                                   && find(owner) != nullptr && GetTickCount64() - began < 30000;
        if (!betweenMovies) presentation.store(false);
        watching.store(betweenMovies || !resource.release());
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    const auto now = GetTickCount64();
    const auto selected = current();
    if (region != heldRegion || step != 38 || arrival != world_revision()
        || !selected || selected->owner != owner) {
        fail("world_changed");
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    if (!frameReady.load()) {
        fail("frame_observer_unavailable");
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    if (!uiReady.load()) {
        fail("movie_presentation_unavailable");
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    if (!resolve()) {
        fail("native_api_unavailable");
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    auto* manager = api.manager();
    auto* decoder = api.decoder();
    if (!manager || !decoder) {
        fail("player_unavailable");
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    if (state == Status::queued) {
        if (!resource.begin(config, movie)) {
            fail("resource_request_failed");
            ReleaseSRWLockExclusive(&lock);
            return;
        }
        if (!resource.advance()) {
            fail("surface_dependency_failed");
            ReleaseSRWLockExclusive(&lock);
            return;
        }
        if (resource.state() == 3 || resource.state() < 0) {
            fail("resource_load_failed");
            ReleaseSRWLockExclusive(&lock);
            return;
        }
        if (!resource.ready()) {
            if (now - began > 30000) fail("resource_ready_timeout");
            ReleaseSRWLockExclusive(&lock);
            return;
        }
        if (api.busy(manager)) {
            if (now - began > 30000) fail("player_busy_timeout");
        } else if (!resource.prepare_surfaces()) {
            if (now - began > 30000) fail("surface_registration_timeout");
        } else {
            // Same acquire/play pairing as the authored pre-rendered component.
            report("resource_ready");
            decoderOwner = decoder;
            api.acquire(manager);
            acquired = true;
            api.play(manager, config.movies[movie - 1].asset, 0);
            state = Status::preparing;
            began = now;
            presentation.store(true);
            report("submitted");
        }
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    if (decoder != decoderOwner) {
        fail("decoder_owner_changed");
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    const auto asset = field<std::uint32_t>(decoder, 0x1B4);
    const int decoderState = field<int>(decoder, 0x1B0);
    if (decoderState != lastDecoderState) {
        report("decoder", decoderState);
        lastDecoderState = decoderState;
    }
    const auto observed =
        playback.observe(asset == config.movies[movie - 1].asset, decoderState, api.busy(manager));
    if (observed == Status::playing && state != Status::playing) report("playing", decoderState);
    // The direct video path has no type-6 source to emit a cinematic-skip incident.
    // A foreground Escape press asks the original native movie player to stop; completion
    // still requires the decoder's subsequent stopped/end receipt for this asset.
    DWORD foregroundProcess{};
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
    const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (observed == Status::playing && foregroundProcess == GetCurrentProcessId() && escapeDown
        && !escapeHeld)
        stopRequested = true;
    escapeHeld = escapeDown;
    if (stopRequested && asset == config.movies[movie - 1].asset) {
        api.stop(manager);
        stopRequested = false;
        report("stop_requested", decoderState);
    }
    if (observed == Status::complete) {
        release();
        if (!continueSequence) presentation.store(false);
        state = Status::complete;
        watching.store(true);
        report("complete", decoderState);
        firstCompleted = owner;
        began = now;
        // Final cleanup runs independently on subsequent frames. Native EOF,
        // not global texture eviction, releases the mission's orbit handoff.
    } else if (observed == Status::failed)
        fail("decoder_failed_or_replaced");
    else
        state = observed;
    if ((state == Status::preparing && now - began > 30000)
        || (state == Status::playing && now - began > 600000))
        fail("playback_timeout");
    ReleaseSRWLockExclusive(&lock);
}
} // namespace sunrise::client::sdk::presentation
