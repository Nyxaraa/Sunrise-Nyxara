#include "runtime.h"
#include "internal.h"
#include "model_channels.h"
#include "../../../core/settings/settings.h"
#include <array>
namespace sunrise::client::sdk::presentation {
bool install() noexcept {
    if (!core::settings::get().server.activation.missionScripting) return true;
    struct Feature {
        StageResult (*stage)(hooking::detour::Spec&) noexcept;
        void (*publish)(const hooking::detour::Handle&) noexcept;
    };
    constexpr std::array features{
        Feature{stage_loading_cinematics, publish_loading_cinematics},
        Feature{stage_movie_tick, publish_movie_tick},
        Feature{stage_movie_ui, publish_movie_ui},
        Feature{stage_movie_hud, publish_movie_hud},
        Feature{stage_effect_attachment, publish_effect_attachment},
    };
    bool ready = true;
    for (const auto& feature : features) {
        hooking::detour::Spec spec{};
        const auto staged = feature.stage(spec);
        if (staged == StageResult::attached) continue;
        if (staged == StageResult::unavailable) { ready = false; continue; }
        hooking::detour::Handle handle{};
        const bool attached = hooking::detour::install(spec, handle);
        feature.publish(handle);
        ready = attached && ready;
    }
    return delivery::install() && ready;
}
void uninstall() noexcept {
    (void)delivery::uninstall();
    uninstall_effect_attachment();
    uninstall_movie_hud();
    uninstall_movie_ui();
    uninstall_movie_tick();
    uninstall_loading_cinematics();
}
} // namespace sunrise::client::sdk::presentation
