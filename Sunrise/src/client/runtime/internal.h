#pragma once

#include <Windows.h>

#include <cstdint>

namespace sunrise::client::runtime {

/** Fixed one-shot activation state for an independently owned hook group. */
enum class StageState : std::uint8_t {
    pending,
    active,
    failed,
};

extern SRWLOCK g_lock;
extern StageState g_mainStage;
extern StageState g_graphicsStage;
extern StageState g_platformStage;
extern HMODULE g_platformModule;
/** Sunrise's own module, kept so activation can resolve the artifact directory. */
extern void* g_sunriseModule;

} // namespace sunrise::client::runtime
