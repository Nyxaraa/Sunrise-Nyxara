#include "resources.h"
#include "readiness_rules.h"
#include <Windows.h>
#include <cstring>
#include "../../patterns/image_scan.h"
#include "../../patterns/signature_text.h"
#include "../../../core/logging/log.h"
namespace sunrise::client::hooks::ember_movies {
namespace {
using namespace patterns;
using Manager=void*(__fastcall*)();
using Create=std::uint32_t*(__fastcall*)(void*,std::uint32_t*,int,int,int,const char*);
using Add=void(__fastcall*)(void*,const std::uint32_t*);
using Submit=void(__fastcall*)(void*,std::uint32_t);
using State=int(__fastcall*)(void*);
Manager manager{}; Create create{}; Add add{}; Submit submit{},destroy{}; State status{};
std::uintptr_t tableGlobal{};
INIT_ONCE init=INIT_ONCE_STATIC_INIT;
bool available{};
template<class T> T read(std::uintptr_t at) { T value{};std::memcpy(&value,reinterpret_cast<void*>(at),sizeof(value));return value; }
void* call(std::byte* code,unsigned offset) {
    if (!code || code[offset]!=std::byte{0xE8}) return nullptr;
    return code+offset+5+read<std::int32_t>(reinterpret_cast<std::uintptr_t>(code+offset+1));
}
bool resolve_native() {
    // B46E10 supplies the root lifecycle API. Its startup assets use kind 2;
    // movies use kind 1, as selected from their package type_info by native 426920.
    constexpr auto loadSig=signature<signature_length("40 53 56 57 48 81 EC 50 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 40 01 00 00 48 63 DA 48 8B F1")>(
        "40 53 56 57 48 81 EC 50 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 40 01 00 00 48 63 DA 48 8B F1");
    // B44020: inspect the completed root, then release it; no global I/O drain needed.
    constexpr auto endSig=signature<signature_length("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20 48 63 EA 4C 8B F1 E8")>(
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20 48 63 EA 4C 8B F1 E8");
    auto* load=scan_main_image_unique(loadSig,"ember_resource_load");
    auto* end=scan_main_image_unique(endSig,"ember_resource_release");
    auto mgr=reinterpret_cast<Manager>(call(load,0x96));
    create=reinterpret_cast<Create>(call(load,0xD1));
    add=reinterpret_cast<Add>(call(load,0x14C));
    submit=reinterpret_cast<Submit>(call(load,0x157));
    status=reinterpret_cast<State>(call(end,0x85));
    destroy=reinterpret_cast<Submit>(call(end,0x9F));
    if (!mgr || !create || !add || !submit || !status || !destroy || !end
        || end[0x2E]!=std::byte{0x48} || end[0x2F]!=std::byte{0x8B} || end[0x30]!=std::byte{0x05}) return false;
    tableGlobal=reinterpret_cast<std::uintptr_t>(end+0x35)+read<std::int32_t>(reinterpret_cast<std::uintptr_t>(end+0x31));
    manager=mgr; return true;
}
BOOL CALLBACK initialize(PINIT_ONCE,void*,void**) { available=resolve_native();return TRUE; }
bool resolve() { InitOnceExecuteOnce(&init,initialize,nullptr,nullptr);return available; }
std::uintptr_t row(std::uint32_t handle) {
    if (handle==0xFFFFFFFFU || !tableGlobal) return 0;
    const auto high=static_cast<std::uint32_t>(static_cast<std::int32_t>(handle)>>13);
    const auto pool=(high&0xFFFFU)&((static_cast<std::uint64_t>(high)|0xFFC0000ULL)>>18);
    const auto head=read<std::uintptr_t>(tableGlobal);
    if (!head) return 0;
    const auto table=read<std::uintptr_t>(head);
    return table ? table+64*pool : 0;
}
std::uintptr_t datum(std::uint32_t handle) {
    const auto pool=row(handle);if (!pool) return 0;
    const auto storage=read<std::uintptr_t>(pool+8);
    const auto stride=read<std::uint32_t>(pool+48);
    if (!storage || !stride) return 0;
    return storage+stride*static_cast<std::uintptr_t>(handle&0x1FFFU);
}
void* blob(std::uint32_t handle,std::uint32_t expectedClass=0) {
    const auto item=datum(handle);if (!item) return nullptr;
    // Unloaded package entries contain FEFE free-list markers, not a definition.
    if (expectedClass && read<std::uint32_t>(item)!=expectedClass) return nullptr;
    const auto mask=static_cast<std::uintptr_t>(static_cast<std::intptr_t>(read<std::int32_t>(row(handle)+52)));
    const auto at=item-(read<std::uintptr_t>(item+8)&mask);
    return reinterpret_cast<void*>(at);
}
bool stream_ready(std::uint32_t handle) {
    const auto item=datum(handle);if (!item) return false;
    const auto mask=static_cast<std::uintptr_t>(static_cast<std::intptr_t>(read<std::int32_t>(row(handle)+52)));
    // Never dereference this value: it encodes the file offset and patch id.
    const auto location=item-(read<std::uintptr_t>(item+8)&mask);
    return movie_stream_ready(read<std::uint32_t>(item),read<std::uint32_t>(item+4),location);
}
}
bool sunburn_resident() noexcept {
    if (!resolve()) return false;
    __try { return blob(0x80B82489U,0x80809C0FU)!=nullptr; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool MovieResource::begin(std::uint32_t asset) noexcept {
    if (held()) return asset_==asset;
    if ((asset!=0x80BCA001U && asset!=0x80BCA003U) || !resolve()) return false;
    auto* mgr=manager(); if (!mgr) return false;
    create(mgr,&root_,8,2,0,"mission_ember_movie");
    if (!held()) return false;
    auto* root=blob(root_);if (!root) return false;
    // Retain metadata plus the compact stream mapping consumed by 41A160.
    // Its native kind-1 load initializes file offset/patch/size without copying the movie.
    for (const auto tag : movie_metadata(asset)) {
        const std::uint32_t request[]{movie_resource_kind,tag};
        add(root,request);
    }
    const std::uint32_t streamRequest[]{movie_resource_kind,movie_stream(asset)};
    add(root,streamRequest);
    submit(mgr,root_);asset_=asset;
    core::log::writef(core::log::Channel::client,core::log::Level::info,
        "ev=ember_movie result=resource_requested asset=%08X root=%08X kind=1 metadata=4 stream=%08X",asset_,root_,movie_stream(asset_));
    return true;
}
int MovieResource::state() const noexcept {
    if (!held() || !status) return -1;
    __try { auto* root=blob(root_);return root ? status(root) : -1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool MovieResource::ready() const noexcept {
    if (state()!=2) return false;
    __try {
        auto* wrapper=blob(asset_,0x80808495U);
        if (!wrapper) return false;
        const auto header=read<std::uint32_t>(reinterpret_cast<std::uintptr_t>(wrapper)+8);
        if (header!=asset_-1) return false;
        auto* info=blob(header,0x80808499U);
        const auto metadata=movie_metadata(asset_);
        if (read<std::uint32_t>(reinterpret_cast<std::uintptr_t>(wrapper)+12)!=metadata[2]
            || !blob(metadata[2],0x80809A88U) || !blob(metadata[3],0x80806B8FU)) return false;
        const auto media=info ? read<std::uint32_t>(reinterpret_cast<std::uintptr_t>(info)+0x18) : 0xFFFFFFFFU;
        return movie_resources_ready(2,asset_,true,header,info!=nullptr,media)
            && media==movie_stream(asset_) && stream_ready(media);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool MovieResource::release() noexcept {
    if (!held()) return true;
    const auto value=state();
    if (!resource_can_release(value)) return false;
    destroy(manager(),root_);
    core::log::writef(core::log::Channel::client,core::log::Level::info,
        "ev=ember_movie result=resource_released asset=%08X root=%08X",asset_,root_);
    root_=0xFFFFFFFFU;asset_=0;return true;
}
}
