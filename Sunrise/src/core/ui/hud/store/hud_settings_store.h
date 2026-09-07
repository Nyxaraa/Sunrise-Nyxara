#pragma once

#include <span>

namespace sunrise::core::ui::hud::store {

/** One overlay's file key and its switch state. */
struct Switch {
    const char* key{};
    bool on{};
};

/**
 * Resolves the switch file. It reads nothing; load does that.
 * @param module Loaded DLL used to resolve the owned artifact directory.
 */
void initialize(void* module) noexcept;

/** Drops the resolved file path. */
void shutdown() noexcept;

/**
 * Applies the saved state over the caller's defaults.
 * @param switches Keys to look for. A state is replaced only by a key the file carries.
 */
void load(std::span<Switch> switches) noexcept;

/**
 * Writes the whole file. It is small enough that a complete rewrite is the simplest save.
 * @param switches Every key and its state, in table order.
 * @return True when every byte reached the file.
 */
bool save(std::span<const Switch> switches) noexcept;

} // namespace sunrise::core::ui::hud::store
