#include "runtime.h"
#include "resources.h"
#include "ui_mask.h"
#include "../../../state/activity/presentation/runtime.h"
#include "../../../state/activity/presentation/movies.h"
#include "../../../core/logging/log.h"
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"
#include <Windows.h>
#include <cstring>
namespace sunrise::client::native::presentation {
namespace {
namespace shared = state::activity::presentation;
using namespace patterns;
using Status = shared::MovieStatus;
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
using ActivityReader = void(__fastcall*)(std::uint16_t*);
using StepReader = std::int64_t(__fastcall*)();
ActivityReader readActivity{};
StepReader readStep{};
using Predicate = bool(__fastcall*)(void*);
using SliceReader = void*(__fastcall*)(void*, std::int32_t*);
Accessor sliceManager{};
Predicate worldPresent{}, sliceAddressable{};
SliceReader currentSlice{};
shared::MovieRequest command{};
MovieResource resource;
Status state{Status::absent};
std::uint64_t began{}, waitingKey{}, waitingSince{};
shared::Owner waitingOwner{};
void* decoderOwner{};
bool acquired{}, attempted{}, seenPlaying{}, stopSent{}, nativeFault{};
bool loadingFault{};
shared::Owner loadingOwner{};
std::uint64_t loadingBegan{};
void report(const char* result) {
    core::log::writef(core::log::Channel::client, core::log::Level::info,
        "ev=movie result=%s movie=%u request=%llu", result, command.index,
        static_cast<unsigned long long>(command.key));
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

bool resolve_world() {
    if (readActivity && readStep && sliceManager && worldPresent && currentSlice && sliceAddressable)
        return true;
    constexpr auto activity = signature<signature_length(
        "48 89 5C 24 ? 55 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? "
        "48 33 C4 48 89 85 ? ? ? ? 48 8B D9 E8 ? ? ? ? 4C 8B C0 33 D2 8B C2 "
        "4D 85 C0 74 ? 49 63 40 10 48 69 C8 A0 C8 01 00")>(
        "48 89 5C 24 ? 55 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? "
        "48 33 C4 48 89 85 ? ? ? ? 48 8B D9 E8 ? ? ? ? 4C 8B C0 33 D2 8B C2 "
        "4D 85 C0 74 ? 49 63 40 10 48 69 C8 A0 C8 01 00");
    constexpr auto step = signature<signature_length(
        "48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 0B 8B 80 90 03 00 00 48 83 C4 28 C3 83 C8 FF 48 83 C4 28 C3")>(
        "48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 0B 8B 80 90 03 00 00 48 83 C4 28 C3 83 C8 FF 48 83 C4 28 C3");
    readActivity = reinterpret_cast<ActivityReader>(scan_main_image_unique(activity, "movie_activity"));
    readStep = reinterpret_cast<StepReader>(scan_main_image_unique(step, "movie_world_step"));
    constexpr auto spawn = signature<signature_length(
        "40 53 57 41 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 8B D9 40 B7 01")>(
        "40 53 57 41 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 8B D9 40 B7 01");
    auto* gate = scan_main_image_unique(spawn, "movie_slice_reader");
    sliceManager = reinterpret_cast<Accessor>(call_target(gate, 0x23));
    worldPresent = reinterpret_cast<Predicate>(call_target(gate, 0x2B));
    currentSlice = reinterpret_cast<SliceReader>(call_target(gate, 0x49));
    sliceAddressable = reinterpret_cast<Predicate>(call_target(gate, 0x51));
    return readActivity && readStep && sliceManager && worldPresent && currentSlice && sliceAddressable;
}
std::int32_t region() {
    auto* manager = sliceManager();
    if (!manager || !worldPresent(manager)) return -1;
    std::int32_t index = -1;
    auto* slice = currentSlice(manager, &index);
    return slice && sliceAddressable(slice) && index >= 0 && index <= 0x1FF ? index : -1;
}
void release_player() {
    if (!acquired) return;
    api.release(api.manager());
    acquired = false;
}
void fail(const char* reason) {
    if (acquired) {
        auto* decoder = api.decoder();
        if (decoder && decoder == decoderOwner
            && field<std::uint32_t>(decoder, 0x1B4) == command.config.movies[command.index - 1].asset)
            api.stop(api.manager());
        release_player();
    }
    state = Status::failed;
    static_cast<void>(set_movie_presentation(false));
    shared::movie_observed(command.owner, command.key, state);
    report(reason);
}
bool active() {
    return state == Status::queued || state == Status::preparing || state == Status::playing;
}
void service_loading() {
    shared::Owner owner{};
    std::uint16_t requested = 0xFFFF;
    bool loadingScreenOnly = false;
    bool mask = shared::loading_mask_request(owner, requested, &loadingScreenOnly);
    const auto now = GetTickCount64();
    if (owner != loadingOwner) {
        static_cast<void>(mask_loading_flight(false));
        loadingOwner = owner;
        loadingBegan = now;
    }
    if (mask) {
        if (!resolve_world()) return;
        std::uint16_t current = 0xFFFF;
        readActivity(&current);
        const auto step = readStep();
        mask = current == requested && step >= 0 && step != 38 && now - loadingBegan < 120000;
        if (current == requested && step == 38) shared::observe_launch(owner, false, true);
        if (mask && loadingScreenOnly) mask = loading_screen_selected();
    }
    static_cast<void>(mask_loading_flight(mask));
}
void service_request() {
    shared::MovieRequest next{};
    if (!shared::movie_request(next)) return;
    if (nativeFault) {
        shared::movie_observed(next.owner, next.key, Status::failed);
        return;
    }
    const auto now = GetTickCount64();
    if (next.owner != command.owner || next.key != command.key) {
        if (active()) { fail("superseded"); return; }
        if (next.status == Status::failed) {
            static_cast<void>(set_movie_presentation(false));
            static_cast<void>(resource.release());
            return;
        }
        if (next.owner != waitingOwner || next.key != waitingKey) {
            waitingOwner = next.owner; waitingKey = next.key; waitingSince = now;
        }
        if (now - waitingSince >= 30000) {
            shared::movie_observed(next.owner, next.key, Status::failed);
            static_cast<void>(set_movie_presentation(false));
            return;
        }
        const bool chain = state == Status::complete && command.continueSequence
            && next.owner == command.owner && next.index == command.index + 1
            && next.region == command.region && next.activity == command.activity
            && now - began < 30000;
        if (!chain && !resource.release()) return;
        command = next;
        state = Status::queued;
        began = now;
        decoderOwner = nullptr;
        seenPlaying = stopSent = false;
    }
    if (!active()) {
        if (!next.registered || !command.continueSequence || now - began >= 30000) {
            static_cast<void>(set_movie_presentation(false));
            static_cast<void>(resource.release());
        } else if (!set_movie_presentation(true)) fail("presentation_unavailable");
        return;
    }
    if (!next.registered) { fail("owner_removed"); return; }
    if (!resolve() || !resolve_world()) { fail("native_api_unavailable"); return; }
    std::uint16_t activity = 0xFFFF;
    readActivity(&activity);
    if (activity != command.activity || readStep() != 38
        || region() != command.region) {
        fail("world_changed"); return;
    }
    auto* manager = api.manager();
    auto* decoder = api.decoder();
    if (!manager || !decoder) { fail("player_unavailable"); return; }
    if (state != Status::queued && !set_movie_presentation(true)) {
        fail("presentation_unavailable"); return;
    }
    if (state == Status::queued) {
        if (next.stop) { fail("cancelled_before_playback"); return; }
        if (!resource.begin(command.config, command.index) || !resource.advance()) {
            fail("resource_request_failed"); return;
        }
        const int loaded = resource.state();
        if (loaded < 0 || loaded == 3) { fail("resource_load_failed"); return; }
        if (resource.ready() && !api.busy(manager) && resource.prepare_surfaces()) {
            if (!set_movie_presentation(true)) { fail("presentation_unavailable"); return; }
            decoderOwner = decoder;
            api.acquire(manager);
            acquired = true;
            api.play(manager, command.config.movies[command.index - 1].asset, 0);
            state = Status::preparing;
            began = now;
            shared::movie_observed(command.owner, command.key, state);
            report("submitted");
        } else if (now - began >= 30000) fail("resource_ready_timeout");
        return;
    }
    if (decoder != decoderOwner) { fail("decoder_owner_changed"); return; }
    const auto asset = field<std::uint32_t>(decoder, 0x1B4);
    const auto decoderState = field<int>(decoder, 0x1B0);
    if (asset != command.config.movies[command.index - 1].asset) {
        if (seenPlaying) fail("movie_replaced");
        else if (now - began >= 30000) fail("playback_start_timeout");
        return;
    }
    const bool busy = api.busy(manager);
    if (decoderState == 5) seenPlaying = true;
    if (!busy && decoderState != 0 && decoderState != 6) { fail("decoder_failed"); return; }
    if (seenPlaying && !busy) {
        release_player();
        state = Status::complete;
        began = now;
        shared::movie_observed(command.owner, command.key, state);
        report("complete");
        if (!command.continueSequence) static_cast<void>(set_movie_presentation(false));
        return;
    }
    if (seenPlaying && state != Status::playing) {
        state = Status::playing;
        shared::movie_observed(command.owner, command.key, state);
        report("playing");
    }
    // Keyboard confirmation belongs to the native movie UI; Lua may still request a stop.
    if (!stopSent && next.stop) {
        api.stop(manager);
        stopSent = true;
    }
    if (now - began >= (seenPlaying ? 600000 : 30000)) fail("playback_timeout");
}
}
void service() noexcept {
    static thread_local bool servicing{};
    if (servicing) return;
    servicing = true;
    if (!loadingFault) {
        __try { service_loading(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            loadingFault = true;
            core::log::write(core::log::Channel::client, core::log::Level::error,
                             "ev=loading_mask result=native_call_failed");
        }
    }
    __try { service_request(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Ownership may be partially acquired. Do not retry or release uncertain native objects.
        nativeFault = true;
        state = Status::failed;
        shared::movie_observed(command.owner, command.key, state);
        report("native_call_failed");
    }
    servicing = false;
}
}
