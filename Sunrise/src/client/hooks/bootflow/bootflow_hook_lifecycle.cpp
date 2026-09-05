#include "bootflow_hook_lifecycle.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <span>

#include "internal.h"

namespace sunrise::client::hooks::bootflow {
namespace {

std::atomic_bool g_installed{false};

/** One boot-step fix that attaches a detour, in the order the group installs them. */
struct Fix {
    StageResult (*stage)(hooking::detour::Spec&) noexcept;
    void (*publish)(const hooking::detour::Handle&) noexcept;
};

/**
 * Every fix that attaches a detour. `world_step` and `fade_release` are absent: they only find
 * addresses to call, so they open no transaction and cost the group nothing.
 */
constexpr std::array kFixes{
    Fix{&stage_character_select_hold, &publish_character_select_hold},
    Fix{&stage_orbit_slice_set, &publish_orbit_slice_set},
    Fix{&stage_profile_setup_skip, &publish_profile_setup_skip},
    Fix{&stage_composition_check, &publish_composition_check},
    Fix{&stage_orbit_handoff, &publish_orbit_handoff},
    Fix{&stage_owner_activity_slot, &publish_owner_activity_slot},
    Fix{&stage_region_private, &publish_region_private},
    Fix{&stage_spawn_hold, &publish_spawn_hold},
};

/** Marks a fix that staged nothing, so no handle is ever published to it. */
constexpr std::size_t kNotStaged = kFixes.size();

/** One fix's place in the batch, and what it already was before staging. */
struct Placement {
    std::size_t slot{kNotStaged};
    StageResult result{StageResult::unavailable};
};

} // namespace

/**
 * Attaches the boot-step fixes that carry sign-in through to orbit.
 * Each fix stands alone at one site, so a miss on one is reported and the others still attach.
 *
 * Every resolved fix attaches in one transaction rather than one each. A transaction enlists the
 * threads it must suspend by walking every thread on the system, which is far more work than the
 * attach itself, so nine transactions cost nine of those walks and one costs one. A fix whose
 * target is missing simply is not in the batch, which is what keeps one miss off the others. If
 * the batch itself fails the fixes are retried one at a time, so a single target Detours refuses
 * cannot take the rest of the group down with it.
 * @return True when every fix attached.
 */
bool install() noexcept {
    std::array<hooking::detour::Spec, kFixes.size()> specs{};
    std::array<hooking::detour::Handle, kFixes.size()> handles{};
    std::array<Placement, kFixes.size()> placement{};
    std::size_t staged = 0;
    for (std::size_t index = 0; index < kFixes.size(); ++index) {
        hooking::detour::Spec spec{};
        const StageResult result = kFixes[index].stage(spec);
        placement[index].result = result;
        if (result != StageResult::staged) {
            continue;
        }
        specs[staged] = spec;
        placement[index].slot = staged;
        ++staged;
    }

    if (staged != 0
        && !hooking::detour::install(std::span(specs).first(staged),
                                     std::span(handles).first(staged))) {
        // One refused target must not cost the others their fix, so the slow path stands them up
        // separately. It runs only when the whole batch failed, which no supported build does.
        for (std::size_t slot = 0; slot < staged; ++slot) {
            handles[slot] = {};
            (void)hooking::detour::install(specs[slot], handles[slot]);
        }
    }

    bool anyFix = false;
    bool everyFix = true;
    for (std::size_t index = 0; index < kFixes.size(); ++index) {
        const Placement& place = placement[index];
        if (place.slot == kNotStaged) {
            // An already-attached fix stays attached; only a missing target is a failure.
            anyFix = anyFix || place.result == StageResult::attached;
            everyFix = everyFix && place.result == StageResult::attached;
            continue;
        }
        const hooking::detour::Handle& handle = handles[place.slot];
        kFixes[index].publish(handle);
        anyFix = anyFix || handle.attached;
        everyFix = everyFix && handle.attached;
    }

    // Neither of these attaches anything, so they stay outside the transaction.
    const bool worldStep = install_world_step();
    const bool fade = install_fade_release();
    anyFix = anyFix || worldStep || fade;
    g_installed.store(anyFix, std::memory_order_release);
    return everyFix && worldStep && fade;
}

/** Detaches every boot-step fix, in the reverse order of install. */
void uninstall() noexcept {
    uninstall_fade_release();
    uninstall_spawn_hold();
    uninstall_world_step();
    uninstall_region_private();
    uninstall_owner_activity_slot();
    uninstall_orbit_handoff();
    uninstall_composition_check();
    uninstall_profile_setup_skip();
    uninstall_orbit_slice_set();
    uninstall_character_select_hold();
    g_installed.store(false, std::memory_order_release);
}

/** @return True while at least one boot-step fix is attached. */
bool is_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

} // namespace sunrise::client::hooks::bootflow
