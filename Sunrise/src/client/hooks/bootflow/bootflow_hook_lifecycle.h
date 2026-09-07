#pragma once

#include <cstdint>

namespace sunrise::client::hooks::bootflow {

/** One fresh read of the client's local slice-set manager. */
struct CurrentSliceSet final {
    std::int32_t index{-1};
    /** False when the target was not resolved or the frame poll stopped. */
    bool available{};
    /** False while the manager has no addressable current slice set. */
    bool present{};
};

/**
 * Attaches the boot-step fixes that carry sign-in through to character select.
 * Each fix reports its own outcome, so a single miss never disables the others.
 * @return True when every fix attached.
 */
[[nodiscard]] bool install() noexcept;

/** Detaches every boot-step fix. */
void uninstall() noexcept;

/** @return True while at least one boot-step fix is attached. */
[[nodiscard]] bool is_installed() noexcept;

/** Publishes the client's own boot-flow step. Call it per frame, on a game thread. */
void poll_world_step() noexcept;

/** Publishes the client's current local slice set. Call it per frame, on a game thread. */
void poll_current_slice_set() noexcept;

/** @return The last fresh local slice-set read, which any thread may consume. */
[[nodiscard]] CurrentSliceSet current_slice_set() noexcept;

/**
 * Reports whether the player is in a loaded destination.
 * The step is published by a frame poll, so a tick that stops reads as out of world rather than
 * as the last step for ever.
 * @return True while the published step is `activity:in_world` and fresh.
 */
[[nodiscard]] bool in_world() noexcept;

} // namespace sunrise::client::hooks::bootflow
