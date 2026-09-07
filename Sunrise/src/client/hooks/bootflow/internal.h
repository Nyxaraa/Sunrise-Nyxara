#pragma once

#include "../../hooking/detour.h"
#include "../../patterns/image_scan.h"

namespace sunrise::client::hooks::bootflow {

using patterns::resolve_relative;
using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/**
 * How far one boot-step fix got before the group's shared attach.
 * Staging and attaching are split so the whole group holds one detour transaction, not one each.
 * A fix that staged is always published, with a detached handle when the attach did not happen.
 */
enum class StageResult : unsigned char {
    /** The target is missing. The fix reported that itself and staged nothing. */
    unavailable,
    /** An earlier install already attached this fix, so there is nothing to stage. */
    attached,
    /** The spec is filled and the fix wants attaching. */
    staged,
};

/**
 * Stages the character-select hold, which stops the sign-in step auto-selecting.
 * @param spec Receives the target and replacement.
 * @return staged when the target was found, unavailable on a miss.
 */
[[nodiscard]] StageResult stage_character_select_hold(hooking::detour::Spec& spec) noexcept;

/** Takes the character-select hold's attached handle, or a detached one. */
void publish_character_select_hold(const hooking::detour::Handle& handle) noexcept;

/** Detaches the character-select hold. */
void uninstall_character_select_hold() noexcept;

/**
 * Stages the orbit slice-set picker, so the sign-in step's map load finds its target.
 * @param spec Receives the target and replacement.
 * @return staged when the picker was found, unavailable on a miss.
 */
[[nodiscard]] StageResult stage_orbit_slice_set(hooking::detour::Spec& spec) noexcept;

/** Takes the orbit slice-set picker's attached handle, or a detached one. */
void publish_orbit_slice_set(const hooking::detour::Handle& handle) noexcept;

/** Detaches the orbit slice-set picker. */
void uninstall_orbit_slice_set() noexcept;

/**
 * Stages the solo composition fix, which clears the count the matchmaking check rejects.
 * @param spec Receives the target and replacement.
 * @return staged when the target was found, unavailable on a miss.
 */
[[nodiscard]] StageResult stage_composition_check(hooking::detour::Spec& spec) noexcept;

/** Takes the solo composition fix's attached handle, or a detached one. */
void publish_composition_check(const hooking::detour::Handle& handle) noexcept;

/** Detaches the solo composition fix. */
void uninstall_composition_check() noexcept;

/**
 * Stages the orbit handoff release, which stops the destination step parking.
 * @param spec Receives the target and replacement.
 * @return staged when the target was found, unavailable on a miss.
 */
[[nodiscard]] StageResult stage_orbit_handoff(hooking::detour::Spec& spec) noexcept;

/** Takes the orbit handoff release's attached handle, or a detached one. */
void publish_orbit_handoff(const hooking::detour::Handle& handle) noexcept;

/** Detaches the orbit handoff release. */
void uninstall_orbit_handoff() noexcept;

/**
 * Stages the owner activity slot force. It pins the participation record to the replicated
 * snapshot at `comp + 496` instead of the local one at `comp + 1256`.
 * @param spec Receives the target and replacement.
 * @return staged when the target was found, unavailable on a miss.
 */
[[nodiscard]] StageResult stage_owner_activity_slot(hooking::detour::Spec& spec) noexcept;

/** Takes the owner activity slot force's attached handle, or a detached one. */
void publish_owner_activity_slot(const hooking::detour::Handle& handle) noexcept;

/** Detaches the owner activity slot force. */
void uninstall_owner_activity_slot() noexcept;

/**
 * Stages the private-region force, so a public region takes the path a private one takes.
 * A public region otherwise holds its slice-set switch until a public activity host connects.
 * @param spec Receives the target and replacement.
 * @return staged when both targets and the call site were found, unavailable on a miss.
 */
[[nodiscard]] StageResult stage_region_private(hooking::detour::Spec& spec) noexcept;

/** Takes the private-region force's attached handle, or a detached one. */
void publish_region_private(const hooking::detour::Handle& handle) noexcept;

/** Detaches the private-region force. */
void uninstall_region_private() noexcept;

/**
 * Finds the boot-flow step accessor behind `in_world`.
 * Nothing is detoured: the accessor is called, so a miss reads as out of world.
 * @return True when the target was found.
 */
[[nodiscard]] bool install_world_step() noexcept;

/** Clears the boot-flow step accessor it found. */
void uninstall_world_step() noexcept;


} // namespace sunrise::client::hooks::bootflow
