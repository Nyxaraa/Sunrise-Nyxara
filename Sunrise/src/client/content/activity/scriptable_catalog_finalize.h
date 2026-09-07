#pragma once

#include "../../../state/build_data/scriptables/definition.h"

namespace sunrise::client::content::activity::scriptables::internal {

using CancelCheck = bool (*)() noexcept;

/** Joins typed references only when one exact target row is present. */
[[nodiscard]] bool join_references(state::build_data::scriptables::Snapshot& output,
                                   CancelCheck cancel) noexcept;

/** Classifies each complete object identity by its package presence across states. */
[[nodiscard]] bool classify_presence(state::build_data::scriptables::Snapshot& output,
                                     CancelCheck cancel) noexcept;

} // namespace sunrise::client::content::activity::scriptables::internal
