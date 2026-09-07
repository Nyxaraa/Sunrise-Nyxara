#include <Windows.h>

#include <atomic>
#include <cstring>

#include "../../../core/logging/log.h"
#include "config_rules.h"
#include "resources.h"
#include "internal.h"
namespace sunrise::client::sdk::presentation {
namespace {
hooking::detour::Handle hook{};
using Attach = bool(__fastcall*)(void*, std::uint32_t);
std::atomic<unsigned> reports{};
template <class T> T read(const void* pointer) {
    T value{};
    std::memcpy(&value, pointer, sizeof(value));
    return value;
}
std::uint32_t* effect_template(void* component,
                               const presentation::EffectAttachment& effect) noexcept {
    __try {
        auto* self = static_cast<std::byte*>(component);
        if (!presentation::effect_matches(effect,
                                                   presentation::current_region(),
                                                   read<std::uint32_t>(self),
                                                   read<std::uint32_t>(self + 4),
                                                   read<std::uint64_t>(self + 8),
                                                   effect.original))
            return nullptr;
        const auto relative = read<std::int64_t>(self + 0x210);
        if (relative <= 0 || relative > 0x1000) return nullptr;
        auto* request = reinterpret_cast<std::uint32_t*>(self + 0x210 + relative);
        return *request == effect.original ? request : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
presentation::EffectAttachment attachment_rule(void* component) noexcept {
    const auto registration = presentation::current();
    if (!registration) return {};
    const auto& config = registration->config;
    for (unsigned i = 0; i < config.effectCount; ++i) {
        const auto& effect = config.effects[i];
        if (effect.region == presentation::current_region()
            && effect_template(component, effect))
            return effect;
    }
    return {};
}
bool __fastcall attach(void* component, std::uint32_t actor) noexcept {
    const auto original = reinterpret_cast<Attach>(hook.original);
    const auto effect = attachment_rule(component);
    auto* request = effect.replacement ? effect_template(component, effect) : nullptr;
    if (!request) return original(component, actor);
    if (!presentation::effect_resident(effect.replacement)) {
        if (reports.fetch_add(1) < 8)
            core::log::writef(core::log::Channel::client,
                              core::log::Level::warn,
                              "ev=effect_attachment result=resource_unavailable asset=%08X",
                              effect.replacement);
        return false;
    }
    const auto previous = *request;
    bool result{};
    // This is the writable, per-component spawn request, NOT shared package data.
    // 9F2760 -> 56DE00 consumes the asset at request+0 and creates a tracked child of this actor.
    // Preserve native duplicate checks, attachment registration and detach behavior.
    *request = effect.replacement;
    __try {
        result = original(component, actor);
    } __finally {
        *request = previous;
    }
    if (result && reports.fetch_add(1) < 16)
        core::log::writef(core::log::Channel::client,
                          core::log::Level::info,
                          "ev=effect_attachment result=attached actor=%08X asset=%08X",
                          actor,
                          effect.replacement);
    return result;
}
} // namespace
StageResult stage_effect_attachment(hooking::detour::Spec& spec) noexcept {
    if (hook.attached) return StageResult::attached;
    constexpr auto sig = signature<signature_length(
        "48 89 5C 24 18 57 48 83 EC 20 8B C2 8B DA 25 FF 1F 00 00 48 8B F9 44 8B C0 8B D0 49 C1 E8 05")>(
        "48 89 5C 24 18 57 48 83 EC 20 8B C2 8B DA 25 FF 1F 00 00 48 8B F9 44 8B C0 8B D0 49 C1 E8 05");
    auto* target = scan_main_image_unique(sig, "effect_attachment_attach");
    if (!target) return StageResult::unavailable;
    spec = {target, reinterpret_cast<void*>(&attach)};
    return StageResult::staged;
}
void publish_effect_attachment(const hooking::detour::Handle& value) noexcept {
    hook = value;
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     value.attached ? "ev=effect_attachment result=hook_attached"
                                    : "ev=effect_attachment result=hook_failed");
}
void uninstall_effect_attachment() noexcept {
    static_cast<void>(hooking::detour::uninstall(hook));
}
} // namespace sunrise::client::sdk::presentation
