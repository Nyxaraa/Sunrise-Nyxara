#pragma once
#include "../../hooking/detour.h"
#include "../../patterns/image_scan.h"
namespace sunrise::client::sdk::presentation {
using patterns::scan_main_image_unique;
using patterns::resolve_relative;
using patterns::signature;
using patterns::signature_length;
enum class StageResult : unsigned char { unavailable, attached, staged };

StageResult stage_loading_cinematics(hooking::detour::Spec& spec) noexcept;
void publish_loading_cinematics(const hooking::detour::Handle& handle) noexcept;
void uninstall_loading_cinematics() noexcept;
StageResult stage_movie_tick(hooking::detour::Spec& spec) noexcept;
void publish_movie_tick(const hooking::detour::Handle& handle) noexcept;
void uninstall_movie_tick() noexcept;
StageResult stage_movie_ui(hooking::detour::Spec& spec) noexcept;
void publish_movie_ui(const hooking::detour::Handle& handle) noexcept;
void uninstall_movie_ui() noexcept;
StageResult stage_movie_hud(hooking::detour::Spec& spec) noexcept;
void publish_movie_hud(const hooking::detour::Handle& handle) noexcept;
void uninstall_movie_hud() noexcept;
StageResult stage_effect_attachment(hooking::detour::Spec& spec) noexcept;
void publish_effect_attachment(const hooking::detour::Handle& handle) noexcept;
void uninstall_effect_attachment() noexcept;

} // namespace sunrise::client::sdk::presentation
