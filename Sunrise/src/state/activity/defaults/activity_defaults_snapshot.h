#pragma once

#include <string_view>

#include "definition.h"

namespace sunrise::state::activity::defaults {

/** Copies the immutable activity defaults published with the root State. */
void snapshot(ActivityDefaults& output) noexcept;

/**
 * Applies any authored arrival override for one destination onto its selection.
 * @param defaults Authored defaults carrying the override table.
 * @param selection Committed destination, updated in place when a row names it.
 */
void apply_arrival_override(const ActivityDefaults& defaults,
                            destination::DestinationSelection& selection) noexcept;

/**
 * Reads the authored current-activity policy for one destination package.
 * @param packageName Lowercase package name the launch targets.
 * @return True when a launch into it becomes the character's current activity.
 */
[[nodiscard]] bool current_activity_from_launch(std::string_view packageName) noexcept;

} // namespace sunrise::state::activity::defaults
